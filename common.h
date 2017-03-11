#pragma once
#include <signal.h>
int try_function(int ret, const char *file, int line, const char *function, const char *program);

#define try_(x) \
    try_function(x, __FILE__, __LINE__, __FUNCTION__, #x )

sigset_t singleton_set(int signum);
sigset_t get_blocked_signals(void);
int get_childfd(void);
void read_childfd(int childfd, void (*handler)(siginfo_t));
void disable_sigpipe(void);
