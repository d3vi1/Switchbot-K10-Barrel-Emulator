#include "k10_barrel/config.h"

#include <string.h>

static void k10_config_set_defaults(struct k10_config *config) {
    memset(config, 0, sizeof(*config));
    strncpy(config->adapter, "hci0", sizeof(config->adapter) - 1);
    strncpy(config->local_name, "WoS1MB", sizeof(config->local_name) - 1);
    config->company_id = 0x0969;
    config->include_tx_power = true;
    config->fw_major = 1;
    config->fw_minor = 0;
}

int k10_config_load(const char *path, struct k10_config *out_config) {
    (void)path;

    if (out_config == NULL) {
        return -1;
    }

    k10_config_set_defaults(out_config);
    return 0;
}
