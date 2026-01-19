#include "k10_barrel/daemon.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return k10_daemon_run();
}
