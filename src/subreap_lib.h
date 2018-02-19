#pragma once
#include <signal.h>

/* On return, we guarantee that the current process has no more children. */
void filicide(void);

/* Check that this system is configured in such a way that we can
 * actually call filicide() and it will work. */
void sanity_check(void);

/* Returns a signalfd which is readable when we get a signal which is
 * fatal. (and isn't blocked or ignored going into this function.)
 * This function also blocks those signals. */
int get_fatalfd(void);

/* Sends a signal to all children, guaranteed. But it iterates /proc,
 * so could be slow. */
void signal_all_children(int signum);

