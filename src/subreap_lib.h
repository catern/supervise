#pragma once
#include <signal.h>

/* On return, we guarantee that the current process has no more children. */
void filicide(void);

/* Exercise filicide to see if it works properly on this system.
 * We don't run real filicide, because it's fairly expensive.
*/
void trial_filicide(void);

/* Returns a signalfd which is readable when we get a signal which is
 * fatal. (and isn't blocked or ignored going into this function.)
 * This function also blocks those signals. */
int get_fatalfd(void);

/* Sends a signal to all children, guaranteed. But it iterates /proc,
 * so could be slow. */
void signal_all_children(int signum);

