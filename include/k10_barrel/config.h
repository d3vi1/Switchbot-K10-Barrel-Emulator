#ifndef K10_BARREL_CONFIG_H
#define K10_BARREL_CONFIG_H

#include <stdbool.h>

#define K10_MAX_UUIDS 8

struct k10_config {
    char adapter[16];
    char local_name[64];
    unsigned int company_id;
    char manufacturer_mac_label[32];
    char service_uuids[K10_MAX_UUIDS][64];
    unsigned int service_uuid_count;
    char fd3d_service_data_hex[64];
    bool include_tx_power;
    unsigned int fw_major;
    unsigned int fw_minor;
};

int k10_config_load(const char *path, struct k10_config *out_config);
int k10_config_save(const char *path, const struct k10_config *config);

#endif
