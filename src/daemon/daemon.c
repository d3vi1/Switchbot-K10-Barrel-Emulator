#include "k10_barrel/daemon.h"
#include "k10_barrel/config.h"
#include "k10_barrel/log.h"

#define K10_DEFAULT_CONFIG_PATH "/etc/k10-barrel-emulator/config.toml"

int k10_daemon_run(void) {
    struct k10_config config;

    if (k10_config_load(K10_DEFAULT_CONFIG_PATH, &config) != 0) {
        k10_log_error("failed to load config: %s", K10_DEFAULT_CONFIG_PATH);
        return 1;
    }

    k10_log_info("daemon start: adapter=%s name=%s", config.adapter, config.local_name);
    return 0;
}
