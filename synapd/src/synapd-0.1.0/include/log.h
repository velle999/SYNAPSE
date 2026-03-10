#ifndef LOG_H
#define LOG_H
#include <syslog.h>

void syn_log_init(int level);
void syn_log(int priority, const char *fmt, ...) __attribute__((format(printf,2,3)));
#endif
