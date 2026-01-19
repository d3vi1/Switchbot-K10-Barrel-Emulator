#include "k10_barrel/dbus_defs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#define K10_DEFAULT_MODE "barrel"

static void k10_print_usage(const char *name) {
    fprintf(stderr,
            "Usage: %s <command> [options]\n\n"
            "Commands:\n"
            "  status [--mode sweeper|barrel]\n"
            "  start [--mode sweeper|barrel]\n"
            "  stop [--mode sweeper|barrel]\n"
            "  reload [--mode sweeper|barrel]\n"
            "  config get\n"
            "  config set <key> <value> [--type string|uint|bool|list]\n"
            "  config reload\n",
            name);
}

static const char *k10_mode_iface(const char *mode) {
    if (mode != NULL && strcmp(mode, "sweeper") == 0) {
        return K10_DBUS_IFACE_SWEEPER;
    }

    return K10_DBUS_IFACE_BARREL;
}

static int k10_parse_bool(const char *value, bool *out_value) {
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out_value = true;
        return 0;
    }

    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out_value = false;
        return 0;
    }

    return -1;
}

static int k10_parse_uint(const char *value, unsigned int *out_value) {
    char *end = NULL;
    unsigned long parsed = 0;

    errno = 0;
    parsed = strtoul(value, &end, 0);
    if (errno != 0 || end == value) {
        return -1;
    }

    while (*end != '\0') {
        if (*end != ' ' && *end != '\t') {
            return -1;
        }
        end++;
    }

    *out_value = (unsigned int)parsed;
    return 0;
}

static int k10_open_bus(sd_bus **bus) {
    int r = sd_bus_default_system(bus);

    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        return r;
    }

    return 0;
}

static int k10_call_simple(sd_bus *bus, const char *interface, const char *method) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(bus, K10_DBUS_SERVICE, K10_DBUS_OBJECT, interface, method, &error,
                               &reply, "");

    if (r < 0) {
        fprintf(stderr, "D-Bus call failed: %s\n", error.message ? error.message : strerror(-r));
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

static int k10_print_variant(sd_bus_message *m) {
    char type = 0;
    const char *contents = NULL;
    int r = sd_bus_message_peek_type(m, &type, &contents);

    if (r < 0) {
        return r;
    }

    if (type == 's') {
        const char *value = NULL;
        r = sd_bus_message_read(m, "s", &value);
        if (r < 0) {
            return r;
        }
        printf("%s", value ? value : "");
    } else if (type == 'b') {
        int value = 0;
        r = sd_bus_message_read(m, "b", &value);
        if (r < 0) {
            return r;
        }
        printf("%s", value ? "true" : "false");
    } else if (type == 'u') {
        uint32_t value = 0;
        r = sd_bus_message_read(m, "u", &value);
        if (r < 0) {
            return r;
        }
        printf("%u", value);
    } else if (type == 'a' && contents != NULL && strcmp(contents, "s") == 0) {
        const char *item = NULL;
        bool first = true;

        r = sd_bus_message_enter_container(m, 'a', "s");
        if (r < 0) {
            return r;
        }

        while ((r = sd_bus_message_read(m, "s", &item)) > 0) {
            if (!first) {
                printf(", ");
            }
            printf("%s", item);
            first = false;
        }

        if (r < 0) {
            return r;
        }

        r = sd_bus_message_exit_container(m);
        if (r < 0) {
            return r;
        }
    } else {
        fprintf(stderr, "<unsupported type>\n");
        return -1;
    }

    return 0;
}

static int k10_print_dict(sd_bus_message *reply) {
    int r = sd_bus_message_enter_container(reply, 'a', "{sv}");

    if (r < 0) {
        return r;
    }

    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;

        r = sd_bus_message_read(reply, "s", &key);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_enter_container(reply, 'v', NULL);
        if (r < 0) {
            return r;
        }

        printf("%s=", key);
        r = k10_print_variant(reply);
        if (r < 0) {
            return r;
        }
        printf("\n");

        r = sd_bus_message_exit_container(reply);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_exit_container(reply);
        if (r < 0) {
            return r;
        }
    }

    if (r < 0) {
        return r;
    }

    return sd_bus_message_exit_container(reply);
}

static int k10_call_get_dict(sd_bus *bus, const char *interface, const char *method) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(bus, K10_DBUS_SERVICE, K10_DBUS_OBJECT, interface, method, &error,
                               &reply, "");

    if (r < 0) {
        fprintf(stderr, "D-Bus call failed: %s\n", error.message ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return r;
    }

    r = k10_print_dict(reply);
    if (r < 0) {
        fprintf(stderr, "Failed to parse response: %s\n", strerror(-r));
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

static int k10_append_string_array(sd_bus_message *m, const char *value) {
    char *copy = NULL;
    char *token = NULL;
    char *state = NULL;
    int r = 0;

    copy = strdup(value ? value : "");
    if (copy == NULL) {
        return -ENOMEM;
    }

    r = sd_bus_message_open_container(m, 'v', "as");
    if (r < 0) {
        free(copy);
        return r;
    }

    r = sd_bus_message_open_container(m, 'a', "s");
    if (r < 0) {
        free(copy);
        return r;
    }

    for (token = strtok_r(copy, ",", &state); token != NULL; token = strtok_r(NULL, ",", &state)) {
        while (*token == ' ' || *token == '\t') {
            token++;
        }
        if (*token == '\0') {
            continue;
        }
        r = sd_bus_message_append(m, "s", token);
        if (r < 0) {
            free(copy);
            return r;
        }
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        free(copy);
        return r;
    }

    r = sd_bus_message_close_container(m);
    free(copy);
    return r;
}

static int k10_call_set_config(sd_bus *bus, const char *key, const char *value, const char *type) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    int r = 0;

    r = sd_bus_message_new_method_call(bus, &m, K10_DBUS_SERVICE, K10_DBUS_OBJECT,
                                       K10_DBUS_IFACE_CONFIG, "SetConfig");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_append(m, "s", key);
    if (r < 0) {
        goto finish;
    }

    if (type == NULL || strcmp(type, "string") == 0) {
        r = sd_bus_message_append(m, "v", "s", value);
    } else if (strcmp(type, "uint") == 0) {
        unsigned int parsed = 0;
        if (k10_parse_uint(value, &parsed) != 0) {
            fprintf(stderr, "Invalid uint value: %s\n", value);
            r = -EINVAL;
        } else {
            r = sd_bus_message_append(m, "v", "u", parsed);
        }
    } else if (strcmp(type, "bool") == 0) {
        bool parsed = false;
        if (k10_parse_bool(value, &parsed) != 0) {
            fprintf(stderr, "Invalid bool value: %s\n", value);
            r = -EINVAL;
        } else {
            r = sd_bus_message_append(m, "v", "b", parsed ? 1 : 0);
        }
    } else if (strcmp(type, "list") == 0) {
        r = k10_append_string_array(m, value);
    } else {
        fprintf(stderr, "Unknown type: %s\n", type);
        r = -EINVAL;
    }

    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_call(bus, m, 0, &error, &reply);
    if (r < 0) {
        fprintf(stderr, "D-Bus call failed: %s\n", error.message ? error.message : strerror(-r));
        goto finish;
    }

    r = sd_bus_message_read(reply, "b", &r);
    if (r < 0) {
        fprintf(stderr, "Invalid reply: %s\n", strerror(-r));
    }

finish:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(m);
    return r;
}

static const char *k10_get_mode(int argc, char **argv, const char *fallback) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
    }

    return fallback;
}

int main(int argc, char **argv) {
    sd_bus *bus = NULL;
    const char *command = NULL;
    int r = 0;

    if (argc < 2) {
        k10_print_usage(argv[0]);
        return 1;
    }

    command = argv[1];
    r = k10_open_bus(&bus);
    if (r < 0) {
        return 1;
    }

    if (strcmp(command, "status") == 0) {
        const char *mode = k10_get_mode(argc - 2, argv + 2, K10_DEFAULT_MODE);
        r = k10_call_get_dict(bus, k10_mode_iface(mode), "GetStatus");
    } else if (strcmp(command, "start") == 0) {
        const char *mode = k10_get_mode(argc - 2, argv + 2, K10_DEFAULT_MODE);
        r = k10_call_simple(bus, k10_mode_iface(mode), "Start");
    } else if (strcmp(command, "stop") == 0) {
        const char *mode = k10_get_mode(argc - 2, argv + 2, K10_DEFAULT_MODE);
        r = k10_call_simple(bus, k10_mode_iface(mode), "Stop");
    } else if (strcmp(command, "reload") == 0) {
        const char *mode = k10_get_mode(argc - 2, argv + 2, K10_DEFAULT_MODE);
        r = k10_call_simple(bus, k10_mode_iface(mode), "Reload");
    } else if (strcmp(command, "config") == 0) {
        if (argc < 3) {
            k10_print_usage(argv[0]);
            r = -EINVAL;
        } else if (strcmp(argv[2], "get") == 0) {
            r = k10_call_get_dict(bus, K10_DBUS_IFACE_CONFIG, "GetConfig");
        } else if (strcmp(argv[2], "set") == 0) {
            const char *type = "string";

            if (argc < 5) {
                k10_print_usage(argv[0]);
                r = -EINVAL;
            } else {
                for (int i = 5; i + 1 < argc; i++) {
                    if (strcmp(argv[i], "--type") == 0) {
                        type = argv[i + 1];
                        break;
                    }
                }

                r = k10_call_set_config(bus, argv[3], argv[4], type);
            }
        } else if (strcmp(argv[2], "reload") == 0) {
            r = k10_call_simple(bus, K10_DBUS_IFACE_CONFIG, "Reload");
        } else {
            k10_print_usage(argv[0]);
            r = -EINVAL;
        }
    } else {
        k10_print_usage(argv[0]);
        r = -EINVAL;
    }

    sd_bus_unref(bus);
    return r < 0 ? 1 : 0;
}
