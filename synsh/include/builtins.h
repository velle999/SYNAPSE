#ifndef BUILTINS_H
#define BUILTINS_H
#include "synsh.h"
int synsh_builtin(synsh_state_t *s, int argc, char **argv);
bool synsh_is_builtin(const char *cmd);
#endif
