#pragma once
#include <signal.h>

/* On return, we guarantee that the current process has no more children. */
void filicide();
/* A signalfd which is readable when we get an otherwise fatal signal;
 * this function also blocks those signals. */
int get_fatalfd(void);
/* Reads fatal signals from the fatalfd, runs filicide(), and allows
 * them to kill us. */
void read_fatalfd(int fatalfd);

/* Sends a signal to all children, guaranteed. But it iterates /proc,
 * so could be slow. */
void signal_all_children(int signum);

