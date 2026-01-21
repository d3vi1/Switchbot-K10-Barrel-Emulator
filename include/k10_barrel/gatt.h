#ifndef K10_BARREL_GATT_H
#define K10_BARREL_GATT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <systemd/sd-bus.h>

#include "k10_barrel/config.h"

struct k10_gatt_state {
    bool registered;
    bool pending;
    bool tx_notifying;
    char app_path[128];
    sd_bus_slot *object_manager_slot;
    sd_bus_slot *service_slot;
    sd_bus_slot *rx_slot;
    sd_bus_slot *tx_slot;
    sd_bus_slot *pending_slot;
    struct k10_config config;
    uint8_t rx_value[512];
    size_t rx_len;
    uint8_t tx_value[512];
    size_t tx_len;
};

int k10_gatt_start(sd_bus *bus, struct k10_gatt_state *state, const struct k10_config *config);
int k10_gatt_stop(sd_bus *bus, struct k10_gatt_state *state);

#endif
