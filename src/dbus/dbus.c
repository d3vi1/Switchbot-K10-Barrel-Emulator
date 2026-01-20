#include "k10_barrel/dbus.h"
#include "k10_barrel/advertising.h"
#include "k10_barrel/config.h"
#include "k10_barrel/gatt.h"
#include "k10_barrel/log.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include "k10_barrel/dbus_defs.h"

struct k10_dbus_context {
    sd_bus *bus;
    struct k10_daemon_state *state;
};

struct k10_control_binding {
    struct k10_dbus_context *ctx;
    enum k10_emulator_mode mode;
};

static volatile sig_atomic_t k10_should_exit = 0;

static const char *k10_mode_to_string(enum k10_emulator_mode mode) {
    switch (mode) {
    case K10_MODE_SWEEPER:
        return "sweeper";
    case K10_MODE_BARREL:
        return "barrel";
    default:
        return "idle";
    }
}

static int k10_dbus_append_kv_string(sd_bus_message *msg, const char *key, const char *value) {
    int r = 0;

    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "s", key);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(msg, 'v', "s");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "s", value);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(msg);
}

static int k10_dbus_append_kv_bool(sd_bus_message *msg, const char *key, bool value) {
    int r = 0;

    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "s", key);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(msg, 'v', "b");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "b", value);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(msg);
}

static int k10_dbus_append_kv_uint(sd_bus_message *msg, const char *key, unsigned int value) {
    int r = 0;

    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "s", key);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(msg, 'v', "u");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "u", value);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(msg);
}

static int k10_dbus_append_kv_string_array(sd_bus_message *msg, const char *key,
                                           const char *values[], unsigned int count) {
    int r = 0;

    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(msg, "s", key);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(msg, 'v', "as");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_open_container(msg, 'a', "s");
    if (r < 0) {
        return r;
    }

    for (unsigned int i = 0; i < count; i++) {
        r = sd_bus_message_append(msg, "s", values[i]);
        if (r < 0) {
            return r;
        }
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(msg);
}

static int k10_dbus_append_status(sd_bus_message *msg, const struct k10_daemon_state *state) {
    int r = 0;

    r = sd_bus_message_open_container(msg, 'a', "{sv}");
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_bool(msg, "running", state->running);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_bool(msg, "advertising", state->adv.registered || state->adv.pending);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "mode", k10_mode_to_string(state->mode));
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "adapter", state->config.adapter);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_close_container(msg);
    if (r < 0) {
        return r;
    }

    return 0;
}

static int k10_dbus_append_config(sd_bus_message *msg, const struct k10_config *config) {
    const char *service_uuids[K10_MAX_UUIDS];
    int r = 0;

    for (unsigned int i = 0; i < config->service_uuid_count; i++) {
        service_uuids[i] = config->service_uuids[i];
    }

    r = sd_bus_message_open_container(msg, 'a', "{sv}");
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "adapter", config->adapter);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "local_name", config->local_name);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_uint(msg, "company_id", config->company_id);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "manufacturer_mac_label", config->manufacturer_mac_label);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string_array(msg, "service_uuids", service_uuids,
                                        config->service_uuid_count);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_string(msg, "fd3d_service_data_hex", config->fd3d_service_data_hex);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_bool(msg, "include_tx_power", config->include_tx_power);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_uint(msg, "fw_major", config->fw_major);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_kv_uint(msg, "fw_minor", config->fw_minor);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(msg);
}

static int k10_dbus_emit_status_changed(struct k10_dbus_context *ctx, const char *interface) {
    sd_bus_message *signal = NULL;
    int r = 0;

    r = sd_bus_message_new_signal(ctx->bus, &signal, K10_DBUS_OBJECT, interface, "StatusChanged");
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_status(signal, ctx->state);
    if (r < 0) {
        sd_bus_message_unref(signal);
        return r;
    }

    r = sd_bus_send(ctx->bus, signal, NULL);
    sd_bus_message_unref(signal);
    return r;
}

static int k10_dbus_emit_config_changed(struct k10_dbus_context *ctx) {
    sd_bus_message *signal = NULL;
    int r = 0;

    r = sd_bus_message_new_signal(ctx->bus, &signal, K10_DBUS_OBJECT, K10_DBUS_IFACE_CONFIG,
                                  "ConfigChanged");
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_config(signal, &ctx->state->config);
    if (r < 0) {
        sd_bus_message_unref(signal);
        return r;
    }

    r = sd_bus_send(ctx->bus, signal, NULL);
    sd_bus_message_unref(signal);
    return r;
}

static void k10_dbus_emit_status_all(struct k10_dbus_context *ctx) {
    k10_dbus_emit_status_changed(ctx, K10_DBUS_IFACE_SWEEPER);
    k10_dbus_emit_status_changed(ctx, K10_DBUS_IFACE_BARREL);
}

static int k10_dbus_reload_config(struct k10_dbus_context *ctx) {
    bool was_advertising = ctx->state->adv.registered;
    bool was_gatt = ctx->state->gatt.registered;

    if (k10_config_load(ctx->state->config_path, &ctx->state->config) != 0) {
        k10_log_error("dbus reload failed: %s", ctx->state->config_path);
        return -1;
    }

    if (was_advertising) {
        k10_adv_stop(ctx->bus, &ctx->state->adv);
        if (k10_adv_start(ctx->bus, &ctx->state->adv, &ctx->state->config) == 0) {
            ctx->state->running = true;
        } else {
            ctx->state->running = false;
        }
    }
    if (was_gatt) {
        k10_gatt_stop(ctx->bus, &ctx->state->gatt);
        if (k10_gatt_start(ctx->bus, &ctx->state->gatt, &ctx->state->config) == 0) {
            ctx->state->running = true;
        } else {
            ctx->state->running = false;
        }
    }

    k10_log_info("dbus reload: %s", ctx->state->config_path);
    k10_dbus_emit_config_changed(ctx);
    k10_dbus_emit_status_all(ctx);
    return 0;
}

static int k10_method_get_status(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_control_binding *binding = userdata;
    sd_bus_message *reply = NULL;
    int r = 0;

    (void)ret_error;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_status(reply, binding->ctx->state);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_send(binding->ctx->bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

static int k10_method_start(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_control_binding *binding = userdata;
    bool ok = false;
    bool adv_ok = false;
    bool gatt_ok = false;

    (void)ret_error;

    adv_ok = (k10_adv_start(binding->ctx->bus, &binding->ctx->state->adv,
                            &binding->ctx->state->config) == 0);
    gatt_ok = (k10_gatt_start(binding->ctx->bus, &binding->ctx->state->gatt,
                              &binding->ctx->state->config) == 0);

    if (adv_ok && gatt_ok) {
        binding->ctx->state->running = true;
        binding->ctx->state->mode = binding->mode;
        ok = true;
    } else {
        if (adv_ok) {
            k10_adv_stop(binding->ctx->bus, &binding->ctx->state->adv);
        }
        if (gatt_ok) {
            k10_gatt_stop(binding->ctx->bus, &binding->ctx->state->gatt);
        }
        binding->ctx->state->running = false;
        binding->ctx->state->mode = K10_MODE_NONE;
    }

    k10_log_info("dbus start requested: mode=%s", k10_mode_to_string(binding->mode));
    k10_dbus_emit_status_all(binding->ctx);

    return sd_bus_reply_method_return(m, "b", ok);
}

static int k10_method_stop(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_control_binding *binding = userdata;

    (void)ret_error;

    k10_adv_stop(binding->ctx->bus, &binding->ctx->state->adv);
    k10_gatt_stop(binding->ctx->bus, &binding->ctx->state->gatt);
    binding->ctx->state->running = false;
    binding->ctx->state->mode = K10_MODE_NONE;

    k10_log_info("dbus stop requested");
    k10_dbus_emit_status_all(binding->ctx);

    return sd_bus_reply_method_return(m, "b", 1);
}

static int k10_method_reload(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_control_binding *binding = userdata;
    int ok = 0;

    (void)ret_error;

    ok = (k10_dbus_reload_config(binding->ctx) == 0);
    return sd_bus_reply_method_return(m, "b", ok);
}

static int k10_method_get_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_dbus_context *ctx = userdata;
    sd_bus_message *reply = NULL;
    int r = 0;

    (void)ret_error;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        return r;
    }

    r = k10_dbus_append_config(reply, &ctx->state->config);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_send(ctx->bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

static int k10_dbus_apply_string(sd_bus_message *m, char *out, size_t out_size) {
    const char *value = NULL;
    int r = 0;

    r = sd_bus_message_enter_container(m, 'v', "s");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_read(m, "s", &value);
    if (r < 0) {
        return r;
    }

    if (value == NULL) {
        value = "";
    }

    strncpy(out, value, out_size - 1);
    out[out_size - 1] = '\0';

    return sd_bus_message_exit_container(m);
}

static int k10_dbus_apply_uint(sd_bus_message *m, unsigned int *out_value) {
    uint32_t value = 0;
    int r = 0;

    r = sd_bus_message_enter_container(m, 'v', "u");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_read(m, "u", &value);
    if (r < 0) {
        return r;
    }

    *out_value = value;
    return sd_bus_message_exit_container(m);
}

static int k10_dbus_apply_bool(sd_bus_message *m, bool *out_value) {
    int r = 0;
    int value = 0;

    r = sd_bus_message_enter_container(m, 'v', "b");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_read(m, "b", &value);
    if (r < 0) {
        return r;
    }

    *out_value = value != 0;
    return sd_bus_message_exit_container(m);
}

static int k10_dbus_apply_uuid_array(sd_bus_message *m, struct k10_config *config) {
    const char *value = NULL;
    unsigned int count = 0;
    int r = 0;

    r = sd_bus_message_enter_container(m, 'v', "as");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) {
        return r;
    }

    while ((r = sd_bus_message_read(m, "s", &value)) > 0) {
        if (count < K10_MAX_UUIDS) {
            strncpy(config->service_uuids[count], value, sizeof(config->service_uuids[0]) - 1);
            config->service_uuids[count][sizeof(config->service_uuids[0]) - 1] = '\0';
            count++;
        }
    }

    if (r < 0) {
        return r;
    }

    config->service_uuid_count = count;

    r = sd_bus_message_exit_container(m);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_exit_container(m);
}

static int k10_method_set_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_dbus_context *ctx = userdata;
    struct k10_config updated_config = ctx->state->config;
    int changed = 0;
    int r = 0;

    (void)ret_error;

    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }

    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        bool entry_updated = false;

        r = sd_bus_message_read(m, "s", &key);
        if (r < 0) {
            return r;
        }

        if (strcmp(key, "adapter") == 0) {
            r = k10_dbus_apply_string(m, updated_config.adapter, sizeof(updated_config.adapter));
            entry_updated = (r >= 0);
        } else if (strcmp(key, "local_name") == 0) {
            r = k10_dbus_apply_string(m, updated_config.local_name,
                                      sizeof(updated_config.local_name));
            entry_updated = (r >= 0);
        } else if (strcmp(key, "company_id") == 0) {
            r = k10_dbus_apply_uint(m, &updated_config.company_id);
            entry_updated = (r >= 0);
        } else if (strcmp(key, "manufacturer_mac_label") == 0) {
            r = k10_dbus_apply_string(m, updated_config.manufacturer_mac_label,
                                      sizeof(updated_config.manufacturer_mac_label));
            entry_updated = (r >= 0);
        } else if (strcmp(key, "service_uuids") == 0) {
            r = k10_dbus_apply_uuid_array(m, &updated_config);
            entry_updated = (r >= 0);
        } else if (strcmp(key, "fd3d_service_data_hex") == 0) {
            r = k10_dbus_apply_string(m, updated_config.fd3d_service_data_hex,
                                      sizeof(updated_config.fd3d_service_data_hex));
            entry_updated = (r >= 0);
        } else if (strcmp(key, "include_tx_power") == 0) {
            r = k10_dbus_apply_bool(m, &updated_config.include_tx_power);
            entry_updated = (r >= 0);
        } else if (strcmp(key, "fw_major") == 0) {
            r = k10_dbus_apply_uint(m, &updated_config.fw_major);
            entry_updated = (r >= 0);
        } else if (strcmp(key, "fw_minor") == 0) {
            r = k10_dbus_apply_uint(m, &updated_config.fw_minor);
            entry_updated = (r >= 0);
        } else {
            r = sd_bus_message_skip(m, "v");
        }

        if (r < 0) {
            return r;
        }

        r = sd_bus_message_exit_container(m);
        if (r < 0) {
            return r;
        }

        if (entry_updated) {
            changed = 1;
        }
    }

    if (r < 0) {
        return r;
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0) {
        return r;
    }

    if (changed) {
        ctx->state->config = updated_config;
        if (k10_config_save(ctx->state->config_path, &ctx->state->config) != 0) {
            k10_log_error("dbus config save failed: %s", ctx->state->config_path);
            return sd_bus_reply_method_return(m, "b", 0);
        }

        k10_log_info("dbus config updated");
        k10_dbus_emit_config_changed(ctx);
        k10_dbus_emit_status_all(ctx);
    }

    return sd_bus_reply_method_return(m, "b", 1);
}

static int k10_method_reload_config(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_dbus_context *ctx = userdata;
    int ok = 0;

    (void)ret_error;

    ok = (k10_dbus_reload_config(ctx) == 0);
    return sd_bus_reply_method_return(m, "b", ok);
}

static const sd_bus_vtable k10_control_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Start", "", "b", k10_method_start, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "b", k10_method_stop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Reload", "", "b", k10_method_reload, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus", "", "a{sv}", k10_method_get_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("StatusChanged", "a{sv}", 0),
    SD_BUS_VTABLE_END};

static const sd_bus_vtable k10_config_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetConfig", "", "a{sv}", k10_method_get_config, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetConfig", "a{sv}", "b", k10_method_set_config, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Reload", "", "b", k10_method_reload_config, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("ConfigChanged", "a{sv}", 0),
    SD_BUS_VTABLE_END};

static void k10_handle_signal(int signal_value) {
    (void)signal_value;
    k10_should_exit = 1;
}

int k10_dbus_run(struct k10_daemon_state *state) {
    struct k10_dbus_context ctx = {0};
    struct k10_control_binding sweeper_binding = {0};
    struct k10_control_binding barrel_binding = {0};
    sd_bus_slot *sweeper_slot = NULL;
    sd_bus_slot *barrel_slot = NULL;
    sd_bus_slot *config_slot = NULL;
    int exit_code = 0;
    int r = 0;

    if (state == NULL) {
        return 1;
    }

    ctx.state = state;
    r = sd_bus_default_system(&ctx.bus);
    if (r < 0) {
        k10_log_error("dbus connect failed: %s", strerror(-r));
        return 1;
    }

    r = sd_bus_request_name(ctx.bus, K10_DBUS_SERVICE, 0);
    if (r < 0) {
        k10_log_error("dbus request name failed: %s", strerror(-r));
        sd_bus_unref(ctx.bus);
        return 1;
    }

    sweeper_binding.ctx = &ctx;
    sweeper_binding.mode = K10_MODE_SWEEPER;

    barrel_binding.ctx = &ctx;
    barrel_binding.mode = K10_MODE_BARREL;

    r = sd_bus_add_object_vtable(ctx.bus, &sweeper_slot, K10_DBUS_OBJECT, K10_DBUS_IFACE_SWEEPER,
                                 k10_control_vtable, &sweeper_binding);
    if (r < 0) {
        k10_log_error("dbus add sweeper iface failed: %s", strerror(-r));
        exit_code = 1;
        goto cleanup;
    }

    r = sd_bus_add_object_vtable(ctx.bus, &barrel_slot, K10_DBUS_OBJECT, K10_DBUS_IFACE_BARREL,
                                 k10_control_vtable, &barrel_binding);
    if (r < 0) {
        k10_log_error("dbus add barrel iface failed: %s", strerror(-r));
        exit_code = 1;
        goto cleanup;
    }

    r = sd_bus_add_object_vtable(ctx.bus, &config_slot, K10_DBUS_OBJECT, K10_DBUS_IFACE_CONFIG,
                                 k10_config_vtable, &ctx);
    if (r < 0) {
        k10_log_error("dbus add config iface failed: %s", strerror(-r));
        exit_code = 1;
        goto cleanup;
    }

    signal(SIGINT, k10_handle_signal);
    signal(SIGTERM, k10_handle_signal);

    while (!k10_should_exit) {
        r = sd_bus_process(ctx.bus, NULL);
        if (r < 0) {
            k10_log_error("dbus process failed: %s", strerror(-r));
            exit_code = 1;
            break;
        }

        if (r > 0) {
            continue;
        }

        r = sd_bus_wait(ctx.bus, (uint64_t)-1);
        if (r < 0) {
            k10_log_error("dbus wait failed: %s", strerror(-r));
            exit_code = 1;
            break;
        }
    }

cleanup:
    sd_bus_slot_unref(config_slot);
    sd_bus_slot_unref(barrel_slot);
    sd_bus_slot_unref(sweeper_slot);
    sd_bus_unref(ctx.bus);
    return exit_code;
}
