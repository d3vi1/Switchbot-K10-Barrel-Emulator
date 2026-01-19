#include "k10_barrel/daemon.h"
#include "k10_barrel/config.h"
#include "k10_barrel/dbus.h"
#include "k10_barrel/log.h"

#include <string.h>

#define K10_DEFAULT_CONFIG_PATH "/etc/k10-barrel-emulator/config.toml"

int k10_daemon_run(void) {
    struct k10_daemon_state state;

    memset(&state, 0, sizeof(state));
    strncpy(state.config_path, K10_DEFAULT_CONFIG_PATH, sizeof(state.config_path) - 1);

    if (k10_config_load(state.config_path, &state.config) != 0) {
        k10_log_error("failed to load config: %s", K10_DEFAULT_CONFIG_PATH);
        return 1;
    }

    k10_log_info("daemon start: adapter=%s name=%s", state.config.adapter, state.config.local_name);

    return k10_dbus_run(&state);
}
