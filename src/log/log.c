#include "k10_barrel/log.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef K10_USE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

static void k10_log_vprint(FILE *stream, const char *level, const char *format, va_list args) {
#ifdef K10_USE_SYSTEMD
    sd_journal_printv(level, format, args);
    (void)stream;
#else
    fprintf(stream, "%s: ", level);
    vfprintf(stream, format, args);
    fputc('\n', stream);
#endif
}

void k10_log_info(const char *format, ...) {
    va_list args;

    va_start(args, format);
    k10_log_vprint(stdout, "INFO", format, args);
    va_end(args);
}

void k10_log_error(const char *format, ...) {
    va_list args;

    va_start(args, format);
    k10_log_vprint(stderr, "ERROR", format, args);
    va_end(args);
}
