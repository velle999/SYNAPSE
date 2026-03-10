#include "synapd.h"
#include "log.h"
#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include <stdio.h>
#include <stdarg.h>

static int g_level = LOG_INFO;

void syn_log_init(int level) {
    g_level = level;
    openlog("synapd", LOG_PID | LOG_CONS, LOG_DAEMON);
}

void syn_log(int priority, const char *fmt, ...) {
    va_list ap;
    if (priority > g_level) return;
    va_start(ap, fmt);
    vfprintf(priority <= LOG_WARNING ? stderr : stdout, fmt, ap);
    fprintf(priority <= LOG_WARNING ? stderr : stdout, "\n");
    va_end(ap);
    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}

void synapd_reload_config(synapd_state_t *s) {
    syn_log(LOG_INFO, "synapd: reloading config (stub)");
    (void)s;
}

void sd_notify_ready(void) {
#ifdef HAVE_LIBSYSTEMD
    sd_notify(0, "READY=1");
#endif
}
