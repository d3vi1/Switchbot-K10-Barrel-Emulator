#include "k10_barrel/gatt.h"

#include "k10_barrel/dbus_defs.h"
#include "k10_barrel/log.h"

#include <errno.h>
#include <string.h>

#include <systemd/sd-bus.h>

#define K10_BLUEZ_SERVICE "org.bluez"
#define K10_GATT_MGR_IFACE "org.bluez.GattManager1"
#define K10_GATT_SERVICE_IFACE "org.bluez.GattService1"
#define K10_GATT_CHAR_IFACE "org.bluez.GattCharacteristic1"

#define K10_GATT_APP_PATH "/ro/vilt/SwitchbotBleEmulator"
#define K10_GATT_SERVICE_PATH "/ro/vilt/SwitchbotBleEmulator/service0"
#define K10_GATT_CHAR_RX_PATH "/ro/vilt/SwitchbotBleEmulator/char_rx"
#define K10_GATT_CHAR_TX_PATH "/ro/vilt/SwitchbotBleEmulator/char_tx"

#define K10_GATT_SERVICE_UUID "CBA20D00-224D-11E6-9FB8-0002A5D5C51B"
#define K10_GATT_CHAR_RX_UUID "CBA20002-224D-11E6-9FB8-0002A5D5C51B"
#define K10_GATT_CHAR_TX_UUID "CBA20003-224D-11E6-9FB8-0002A5D5C51B"

enum k10_gatt_char_kind { K10_CHAR_RX = 0, K10_CHAR_TX = 1 };

struct k10_gatt_char_ctx {
    struct k10_gatt_state *state;
    enum k10_gatt_char_kind kind;
    const char *path;
    const char *uuid;
    const char *service_path;
    const char *flags[4];
    unsigned int flag_count;
};

static struct k10_gatt_char_ctx k10_rx_ctx;
static struct k10_gatt_char_ctx k10_tx_ctx;

static int k10_gatt_service_get_uuid(sd_bus *bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "s", K10_GATT_SERVICE_UUID);
}

static int k10_gatt_service_get_primary(sd_bus *bus, const char *path, const char *interface,
                                        const char *property, sd_bus_message *reply, void *userdata,
                                        sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "b", true);
}

static int k10_gatt_service_get_includes(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "o");
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(reply);
}

static int k10_gatt_char_get_uuid(sd_bus *bus, const char *path, const char *interface,
                                  const char *property, sd_bus_message *reply, void *userdata,
                                  sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    return sd_bus_message_append(reply, "s", ctx->uuid);
}

static int k10_gatt_char_get_service(sd_bus *bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    return sd_bus_message_append(reply, "o", ctx->service_path);
}

static int k10_gatt_char_get_flags(sd_bus *bus, const char *path, const char *interface,
                                   const char *property, sd_bus_message *reply, void *userdata,
                                   sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;
    int r = 0;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    for (unsigned int i = 0; i < ctx->flag_count; i++) {
        r = sd_bus_message_append(reply, "s", ctx->flags[i]);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_gatt_char_get_notifying(sd_bus *bus, const char *path, const char *interface,
                                       const char *property, sd_bus_message *reply, void *userdata,
                                       sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    if (ctx->kind != K10_CHAR_TX) {
        return sd_bus_message_append(reply, "b", false);
    }

    return sd_bus_message_append(reply, "b", ctx->state->tx_notifying);
}

static int k10_gatt_char_get_value(sd_bus *bus, const char *path, const char *interface,
                                   const char *property, sd_bus_message *reply, void *userdata,
                                   sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;
    const uint8_t *value = NULL;
    size_t length = 0;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    if (ctx->kind == K10_CHAR_TX) {
        value = ctx->state->tx_value;
        length = ctx->state->tx_len;
    } else {
        value = ctx->state->rx_value;
        length = ctx->state->rx_len;
    }

    return sd_bus_message_append_array(reply, 'y', value, length);
}

static int k10_gatt_emit_value(sd_bus *bus, const struct k10_gatt_char_ctx *ctx) {
    return sd_bus_emit_properties_changed(bus, ctx->path, K10_GATT_CHAR_IFACE, "Value", NULL);
}

static int k10_gatt_char_read_value(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;
    sd_bus_message *reply = NULL;
    const uint8_t *value = NULL;
    size_t length = 0;
    int r = 0;

    (void)ret_error;

    sd_bus_message_skip(m, "a{sv}");

    if (ctx->kind == K10_CHAR_TX) {
        value = ctx->state->tx_value;
        length = ctx->state->tx_len;
    } else {
        value = ctx->state->rx_value;
        length = ctx->state->rx_len;
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append_array(reply, 'y', value, length);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_send(sd_bus_message_get_bus(m), reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

static void k10_gatt_log_value(const char *label, const uint8_t *value, size_t length) {
    char buffer[1024];
    size_t offset = 0;

    for (size_t i = 0; i < length && offset + 3 < sizeof(buffer); i++) {
        offset += (size_t)snprintf(buffer + offset, sizeof(buffer) - offset, "%02X",
                                   (unsigned int)value[i]);
    }

    buffer[offset] = '\0';
    k10_log_info("%s len=%zu data=%s", label, length, buffer);
}

static int k10_gatt_char_write_value(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;
    const uint8_t *value = NULL;
    size_t length = 0;
    int r = 0;

    if (ctx->kind != K10_CHAR_RX) {
        sd_bus_error_set(ret_error, "org.bluez.Error.NotSupported",
                         "Write is only supported on RX characteristic");
        return -1;
    }

    r = sd_bus_message_read_array(m, 'y', (const void **)&value, &length);
    if (r < 0) {
        return r;
    }
    sd_bus_message_skip(m, "a{sv}");

    if (length > sizeof(ctx->state->rx_value)) {
        length = sizeof(ctx->state->rx_value);
    }

    memcpy(ctx->state->rx_value, value, length);
    ctx->state->rx_len = length;
    k10_gatt_log_value("gatt write (rx)", value, length);

    if (ctx->state->tx_notifying) {
        size_t copy_len = length;
        if (copy_len > sizeof(ctx->state->tx_value)) {
            copy_len = sizeof(ctx->state->tx_value);
        }
        memcpy(ctx->state->tx_value, value, copy_len);
        ctx->state->tx_len = copy_len;
        k10_gatt_emit_value(sd_bus_message_get_bus(m), &k10_tx_ctx);
    }

    return sd_bus_reply_method_return(m, "");
}

static int k10_gatt_char_start_notify(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;

    (void)ret_error;

    if (ctx->kind != K10_CHAR_TX) {
        sd_bus_error_set(ret_error, "org.bluez.Error.NotSupported",
                         "Notify is only supported on TX characteristic");
        return -1;
    }

    ctx->state->tx_notifying = true;
    k10_log_info("gatt notify enabled");
    k10_gatt_emit_value(sd_bus_message_get_bus(m), ctx);
    return sd_bus_reply_method_return(m, "");
}

static int k10_gatt_char_stop_notify(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_gatt_char_ctx *ctx = userdata;

    (void)ret_error;

    if (ctx->kind != K10_CHAR_TX) {
        sd_bus_error_set(ret_error, "org.bluez.Error.NotSupported",
                         "Notify is only supported on TX characteristic");
        return -1;
    }

    ctx->state->tx_notifying = false;
    k10_log_info("gatt notify disabled");
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable k10_gatt_service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", k10_gatt_service_get_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", k10_gatt_service_get_primary, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Includes", "ao", k10_gatt_service_get_includes, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

static const sd_bus_vtable k10_gatt_char_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", k10_gatt_char_read_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", k10_gatt_char_write_value,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartNotify", "", "", k10_gatt_char_start_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", k10_gatt_char_stop_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("UUID", "s", k10_gatt_char_get_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", k10_gatt_char_get_service, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", k10_gatt_char_get_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Notifying", "b", k10_gatt_char_get_notifying, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Value", "ay", k10_gatt_char_get_value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END};

static int k10_gatt_register_complete(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_gatt_state *state = userdata;

    if (ret_error != NULL && sd_bus_error_is_set(ret_error)) {
        k10_log_error("gatt register failed: %s",
                      ret_error->message ? ret_error->message : ret_error->name);
        state->registered = false;
    } else if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *error = sd_bus_message_get_error(m);
        k10_log_error("gatt register failed: %s",
                      error && error->message ? error->message : "unknown error");
        state->registered = false;
    } else {
        state->registered = true;
        k10_log_info("gatt registered on %s", state->config.adapter);
    }

    state->pending = false;
    sd_bus_slot_unref(state->pending_slot);
    state->pending_slot = NULL;
    return 1;
}

static int k10_gatt_register_async(sd_bus *bus, struct k10_gatt_state *state, const char *adapter) {
    sd_bus_message *message = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_message_new_method_call(bus, &message, K10_BLUEZ_SERVICE, adapter_path,
                                       K10_GATT_MGR_IFACE, "RegisterApplication");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(message, "o", state->app_path);
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

    r = sd_bus_call_async(bus, &state->pending_slot, message, k10_gatt_register_complete, state, 0);

finish:
    sd_bus_message_unref(message);
    return r;
}

static int k10_gatt_unregister(sd_bus *bus, const struct k10_gatt_state *state,
                               const char *adapter) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_call_method(bus, K10_BLUEZ_SERVICE, adapter_path, K10_GATT_MGR_IFACE,
                           "UnregisterApplication", &error, &reply, "o", state->app_path);
    if (r < 0) {
        k10_log_error("gatt unregister failed: %s", error.message ? error.message : strerror(-r));
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

static void k10_gatt_set_defaults(struct k10_gatt_state *state) {
    if (state->app_path[0] == '\0') {
        strncpy(state->app_path, K10_GATT_APP_PATH, sizeof(state->app_path) - 1);
    }
}

int k10_gatt_start(sd_bus *bus, struct k10_gatt_state *state, const struct k10_config *config) {
    static const char *rx_flags[] = {"write", "write-without-response"};
    static const char *tx_flags[] = {"read", "notify"};
    int r = 0;

    if (bus == NULL || state == NULL || config == NULL) {
        return -EINVAL;
    }

    if (state->registered || state->pending) {
        return 0;
    }

    k10_gatt_set_defaults(state);
    state->config = *config;

    if (state->object_manager_slot == NULL) {
        r = sd_bus_add_object_manager(bus, &state->object_manager_slot, state->app_path);
        if (r < 0) {
            k10_log_error("gatt object manager failed: %s", strerror(-r));
            return r;
        }
    }

    if (state->service_slot == NULL) {
        r = sd_bus_add_object_vtable(bus, &state->service_slot, K10_GATT_SERVICE_PATH,
                                     K10_GATT_SERVICE_IFACE, k10_gatt_service_vtable, state);
        if (r < 0) {
            k10_log_error("gatt service vtable failed: %s", strerror(-r));
            return r;
        }
    }

    k10_rx_ctx.state = state;
    k10_rx_ctx.kind = K10_CHAR_RX;
    k10_rx_ctx.path = K10_GATT_CHAR_RX_PATH;
    k10_rx_ctx.uuid = K10_GATT_CHAR_RX_UUID;
    k10_rx_ctx.service_path = K10_GATT_SERVICE_PATH;
    k10_rx_ctx.flags[0] = rx_flags[0];
    k10_rx_ctx.flags[1] = rx_flags[1];
    k10_rx_ctx.flag_count = 2;

    k10_tx_ctx.state = state;
    k10_tx_ctx.kind = K10_CHAR_TX;
    k10_tx_ctx.path = K10_GATT_CHAR_TX_PATH;
    k10_tx_ctx.uuid = K10_GATT_CHAR_TX_UUID;
    k10_tx_ctx.service_path = K10_GATT_SERVICE_PATH;
    k10_tx_ctx.flags[0] = tx_flags[0];
    k10_tx_ctx.flags[1] = tx_flags[1];
    k10_tx_ctx.flag_count = 2;

    if (state->rx_slot == NULL) {
        r = sd_bus_add_object_vtable(bus, &state->rx_slot, K10_GATT_CHAR_RX_PATH,
                                     K10_GATT_CHAR_IFACE, k10_gatt_char_vtable, &k10_rx_ctx);
        if (r < 0) {
            k10_log_error("gatt rx vtable failed: %s", strerror(-r));
            return r;
        }
    }

    if (state->tx_slot == NULL) {
        r = sd_bus_add_object_vtable(bus, &state->tx_slot, K10_GATT_CHAR_TX_PATH,
                                     K10_GATT_CHAR_IFACE, k10_gatt_char_vtable, &k10_tx_ctx);
        if (r < 0) {
            k10_log_error("gatt tx vtable failed: %s", strerror(-r));
            return r;
        }
    }

    r = k10_gatt_register_async(bus, state, config->adapter);
    if (r < 0) {
        k10_log_error("gatt register request failed: %s", strerror(-r));
        return r;
    }

    state->pending = true;
    state->registered = false;
    k10_log_info("gatt register requested on %s", config->adapter);
    return 0;
}

int k10_gatt_stop(sd_bus *bus, struct k10_gatt_state *state) {
    int r = 0;

    if (bus == NULL || state == NULL) {
        return -EINVAL;
    }

    if (!state->registered && !state->pending) {
        return 0;
    }

    if (state->pending_slot != NULL) {
        sd_bus_slot_unref(state->pending_slot);
        state->pending_slot = NULL;
        state->pending = false;
    }

    if (state->registered) {
        r = k10_gatt_unregister(bus, state, state->config.adapter);
    }

    sd_bus_slot_unref(state->tx_slot);
    state->tx_slot = NULL;
    sd_bus_slot_unref(state->rx_slot);
    state->rx_slot = NULL;
    sd_bus_slot_unref(state->service_slot);
    state->service_slot = NULL;
    sd_bus_slot_unref(state->object_manager_slot);
    state->object_manager_slot = NULL;
    state->registered = false;
    state->pending = false;
    state->tx_notifying = false;
    state->rx_len = 0;
    state->tx_len = 0;
    return r;
}
