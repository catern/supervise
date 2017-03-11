/* 
a utility called subreap. subreap will set CHILD_SUBREAPER and start a
single process; when that process exits (or subreap gets a signal),
subreap will kill all its children and exit with the same exit code.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "subreap_lib.h"
#include "common.h"

struct options {
    char *exec_file;
    char **exec_argv;
};

struct options
get_options(int argc, char **argv) {
    if (argc < 2) {
	fprintf(stderr, "Usage: %s [command]", (argv[0] ? argv[0] : "subreap"));
	exit(1);
    }
    struct options opt = {
	.exec_file = argv[1],
	.exec_argv = argv+1,
    };
    return opt;
}

/* TODO
   Maybe I should run subreaper in front of procfd?
   maybe I should combine them into one program after all

   I think procfd should not exit until the fd is closed or it receives a fatal signal.
   And when that happens, it should SIGKILL all children.
 */
int main(int argc, char **argv) {
    disable_sigpipe();
    struct options opt = get_options(argc, argv);

    /* give it a trial run to see if we can do it - it's idempotent, so no worries */
    filicide();

    try_(prctl(PR_SET_CHILD_SUBREAPER, true));
    atexit(filicide);

    sigset_t original_blocked_signals = get_blocked_signals();
    int fatalfd = get_fatalfd();
    int childfd = get_childfd();

    pid_t main_child_pid = try_(fork());
    if (main_child_pid == 0) {
	/* the child will automatically get sigterm when the parent dies */
	/* this is a paltry effort to sill be effective if we get sigkill'd */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	/* restore the signal mask */
	try_(sigprocmask(SIG_SETMASK, &original_blocked_signals, NULL));
	try_(execvp(opt.exec_file, opt.exec_argv));
    }

    struct pollfd pollfds[2] = {
	{ .fd = fatalfd, .events = POLLIN, .revents = 0, },
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
    };
    void handle_child_status(siginfo_t childinfo) {
	if (childinfo.si_pid != main_child_pid) return;

	if (childinfo.si_code == CLD_EXITED ||
	    childinfo.si_code == CLD_KILLED ||
	    childinfo.si_code == CLD_DUMPED) {
	    exit(0);
	}
    }
    for (;;) {
	try_(poll(pollfds, 2, -1));
	if (pollfds[0].revents & POLLIN) read_fatalfd(fatalfd);
	if (pollfds[1].revents & POLLIN) read_childfd(childfd, handle_child_status);
	if ((pollfds[0].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[1].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll");
	}
    }
}
