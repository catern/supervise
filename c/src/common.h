#pragma once
#include <signal.h>
#include <stdio.h>
int try_function(int ret, const char *file, int line, const char *function, const char *program);

/* a minor utility macro to check return codes and exit if <0. */
#define try_(x) \
    try_function(x, __FILE__, __LINE__, __FUNCTION__, #x )

/* Returns a signal set containing only the argument signal. */
sigset_t singleton_set(int signum);
/* Returns the currently blocked signal set. */
sigset_t get_blocked_signals(void);

/* Returns a signalfd which is readable when we get a SIGCHLD.
 * This function also blocks that signal. */
int get_childfd(void);

/* Marks SIGPIPE as ignored. */
void disable_sigpipe(void);
/* Convert passed string to an integer */
int str_to_int(char const* str);

#define _cleanup_(f) __attribute__((__cleanup__(f)))
void cleanup_close_func(int const* fdp);
#define _cleanup_close_ _cleanup_(cleanup_close_func)
void cleanup_fclose_func(FILE* const* filepp);
#define _cleanup_fclose_ _cleanup_(cleanup_fclose_func)
