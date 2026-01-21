#ifndef K10_BARREL_DAEMON_H
#define K10_BARREL_DAEMON_H

#include <stdbool.h>

#include "k10_barrel/advertising.h"
#include "k10_barrel/config.h"
#include "k10_barrel/gatt.h"

enum k10_emulator_mode { K10_MODE_NONE = 0, K10_MODE_SWEEPER, K10_MODE_BARREL };

struct k10_daemon_state {
    struct k10_config config;
    char config_path[256];
    bool running;
    enum k10_emulator_mode mode;
    struct k10_adv_state adv;
    struct k10_gatt_state gatt;
};

int k10_daemon_run(void);

#endif
