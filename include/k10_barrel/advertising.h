#ifndef K10_BARREL_ADVERTISING_H
#define K10_BARREL_ADVERTISING_H

#include <stdbool.h>
#include <stdint.h>

#include <systemd/sd-bus.h>

#include "k10_barrel/config.h"

struct k10_adv_state {
    bool registered;
    bool pending;
    bool include_service_uuids;
    bool include_service_data;
    bool include_manufacturer_data;
    bool include_local_name;
    bool include_tx_power;
    uint8_t mfg_seq;
    int mgmt_fd;
    uint8_t mgmt_instance;
    bool mgmt_active;
    int hci_fd;
    bool hci_active;
    const char *service_uuid_view[K10_MAX_UUIDS];
    unsigned int service_uuid_view_count;
    char object_path[128];
    sd_bus_slot *slot;
    sd_bus_slot *pending_slot;
    struct k10_config config;
};

int k10_adv_start(sd_bus *bus, struct k10_adv_state *state, const struct k10_config *config);
int k10_adv_stop(sd_bus *bus, struct k10_adv_state *state);

#endif
