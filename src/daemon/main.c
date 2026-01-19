#include "k10_barrel/daemon.h"
#include "k10_barrel/log.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    k10_log_info("k10-barrel-emulatord starting");
    return k10_daemon_run();
}
