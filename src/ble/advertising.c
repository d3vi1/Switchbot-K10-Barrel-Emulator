#include "k10_barrel/advertising.h"

#include "k10_barrel/dbus_defs.h"
#include "k10_barrel/log.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define K10_BLUEZ_SERVICE "org.bluez"
#define K10_ADV_IFACE "org.bluez.LEAdvertisement1"
#define K10_ADV_MGR_IFACE "org.bluez.LEAdvertisingManager1"
#define K10_ADV_OBJECT "/ro/vilt/SwitchbotBleEmulator/advertisement0"

struct k10_hex_bytes {
    uint8_t data[64];
    size_t length;
};

static int k10_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static int k10_parse_hex_bytes(const char *hex, struct k10_hex_bytes *out) {
    size_t length = 0;
    int high = -1;

    if (hex == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    for (size_t i = 0; hex[i] != '\0'; i++) {
        char ch = hex[i];
        int value = 0;

        if (ch == ':' || ch == ' ' || ch == '\t') {
            continue;
        }

        value = k10_hex_value(ch);
        if (value < 0) {
            return -1;
        }

        if (high < 0) {
            high = value;
            continue;
        }

        if (length >= sizeof(out->data)) {
            return -1;
        }

        out->data[length++] = (uint8_t)((high << 4) | value);
        high = -1;
    }

    if (high >= 0) {
        return -1;
    }

    out->length = length;
    return 0;
}

static int k10_adv_get_type(sd_bus *bus, const char *path, const char *interface,
                            const char *property, sd_bus_message *reply, void *userdata,
                            sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "s", "peripheral");
}

static int k10_adv_get_service_uuids(sd_bus *bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    for (unsigned int i = 0; i < state->config.service_uuid_count; i++) {
        r = sd_bus_message_append(reply, "s", state->config.service_uuids[i]);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_append_variant_bytes(sd_bus_message *reply, const uint8_t *data, size_t length) {
    int r = sd_bus_message_open_container(reply, 'v', "ay");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append_array(reply, 'y', data, length);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_manufacturer_data(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;
    struct k10_hex_bytes bytes = {0};

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "{qv}");
    if (r < 0) {
        return r;
    }

    if (state->config.manufacturer_mac_label[0] != '\0') {
        if (k10_parse_hex_bytes(state->config.manufacturer_mac_label, &bytes) == 0 &&
            bytes.length > 0) {
            r = sd_bus_message_open_container(reply, 'e', "qv");
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_append(reply, "q", (uint16_t)state->config.company_id);
            if (r < 0) {
                return r;
            }

            r = k10_adv_append_variant_bytes(reply, bytes.data, bytes.length);
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_close_container(reply);
            if (r < 0) {
                return r;
            }
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_service_data(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;
    struct k10_hex_bytes bytes = {0};

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) {
        return r;
    }

    if (state->config.fd3d_service_data_hex[0] != '\0') {
        if (k10_parse_hex_bytes(state->config.fd3d_service_data_hex, &bytes) == 0 &&
            bytes.length > 0) {
            r = sd_bus_message_open_container(reply, 'e', "sv");
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_append(reply, "s", "FD3D");
            if (r < 0) {
                return r;
            }

            r = k10_adv_append_variant_bytes(reply, bytes.data, bytes.length);
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_close_container(reply);
            if (r < 0) {
                return r;
            }
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_includes(sd_bus *bus, const char *path, const char *interface,
                                const char *property, sd_bus_message *reply, void *userdata,
                                sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    if (state->config.include_tx_power) {
        r = sd_bus_message_append(reply, "s", "tx-power");
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_local_name(sd_bus *bus, const char *path, const char *interface,
                                  const char *property, sd_bus_message *reply, void *userdata,
                                  sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    return sd_bus_message_append(reply, "s", state->config.local_name);
}

static int k10_adv_get_discoverable(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "b", 1);
}

static int k10_adv_release(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)ret_error;

    state->registered = false;
    k10_log_info("advertising released by BlueZ");

    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable k10_adv_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", k10_adv_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Type", "s", k10_adv_get_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", k10_adv_get_service_uuids, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ManufacturerData", "a{qv}", k10_adv_get_manufacturer_data, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceData", "a{sv}", k10_adv_get_service_data, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Includes", "as", k10_adv_get_includes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("LocalName", "s", k10_adv_get_local_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Discoverable", "b", k10_adv_get_discoverable, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

static void k10_adv_set_defaults(struct k10_adv_state *state) {
    if (state->object_path[0] == '\0') {
        strncpy(state->object_path, K10_ADV_OBJECT, sizeof(state->object_path) - 1);
    }
}

static int k10_adv_register(sd_bus *bus, struct k10_adv_state *state, const char *adapter) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *message = NULL;
    sd_bus_message *reply = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_message_new_method_call(bus, &message, K10_BLUEZ_SERVICE, adapter_path,
                                       K10_ADV_MGR_IFACE, "RegisterAdvertisement");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(message, "o", state->object_path);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_open_container(message, 'a', "{sv}");
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_close_container(message);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_call(bus, message, 0, &error, &reply);
    if (r < 0) {
        k10_log_error("advertising register failed: %s",
                      error.message ? error.message : strerror(-r));
    }

finish:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(message);
    return r;
}

static int k10_adv_unregister(sd_bus *bus, const struct k10_adv_state *state, const char *adapter) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_call_method(bus, K10_BLUEZ_SERVICE, adapter_path, K10_ADV_MGR_IFACE,
                           "UnregisterAdvertisement", &error, &reply, "o", state->object_path);
    if (r < 0) {
        k10_log_error("advertising unregister failed: %s",
                      error.message ? error.message : strerror(-r));
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

int k10_adv_start(sd_bus *bus, struct k10_adv_state *state, const struct k10_config *config) {
    int r = 0;

    if (bus == NULL || state == NULL || config == NULL) {
        return -EINVAL;
    }

    if (state->registered) {
        return 0;
    }

    k10_adv_set_defaults(state);
    state->config = *config;

    if (state->slot == NULL) {
        r = sd_bus_add_object_vtable(bus, &state->slot, state->object_path, K10_ADV_IFACE,
                                     k10_adv_vtable, state);
        if (r < 0) {
            k10_log_error("advertising vtable failed: %s", strerror(-r));
            return r;
        }
    }

    r = k10_adv_register(bus, state, config->adapter);
    if (r < 0) {
        sd_bus_slot_unref(state->slot);
        state->slot = NULL;
        return r;
    }

    state->registered = true;
    k10_log_info("advertising registered on %s", config->adapter);
    return 0;
}

int k10_adv_stop(sd_bus *bus, struct k10_adv_state *state) {
    int r = 0;

    if (bus == NULL || state == NULL) {
        return -EINVAL;
    }

    if (!state->registered) {
        return 0;
    }

    r = k10_adv_unregister(bus, state, state->config.adapter);
    sd_bus_slot_unref(state->slot);
    state->slot = NULL;
    state->registered = false;

    k10_log_info("advertising unregistered");
    return r;
}
