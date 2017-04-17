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
#include "common.h"
#include "subreap_lib.h"

void handle_command(const char *command, const int main_child_pid) {
    uint32_t signal = -1;
    if (sscanf(command, "signal %u\n", &signal) == 1) {
	if (main_child_pid != -1) {
	    try_(kill(main_child_pid, signal));
	}
    }
    /* I can also safely send signals to all my immediate children.
     * (of which I might have multiple, if something tried to daemonize)
     * I don't see a use for that at the moment, though, so I haven't exposed it.
     */
}

void read_controlfd(const int controlfd, const int main_child_pid) {
    int size;
    char buf[4096] = {};
    while ((size = try_(read(controlfd, &buf, sizeof(buf)-1))) > 0) {
	buf[size] = '\0';
	/* BUG we assume we get full lines, one line at a time */
	/* (that's not as dangerous as it might seem though due to pipe atomicity) */
	handle_command(buf, main_child_pid);
	memset(buf, 0, sizeof(buf));
    }
}

void read_fatalfd(const int fatalfd) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(fatalfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	/* explicitly filicide, since dying from a signal won't call exit handlers */
	filicide();
	/* we will now exit in read_childfd when we see we have no children left */
    }
}

void read_childfd(int childfd, int statusfd, int main_child_pid) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(childfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	siginfo_t childinfo = {};
	for (;;) {
	    childinfo.si_pid = 0;
	    const int ret = waitid(P_ALL, 0, &childinfo, WEXITED|WNOHANG);
	    if (ret == -1 && errno == ECHILD) {
		/* no more children. there's nothing else we can do, so we exit */
		dprintf(statusfd, "no_children\n");
		exit(0);
	    }
	    /* no child was in a waitable state */
	    if (childinfo.si_pid == 0) break;

	    /* we only report information for our main child */
	    if (childinfo.si_pid != main_child_pid) continue;

	    /* stringify the state change and print it */
	    if (childinfo.si_code == CLD_EXITED) {
		dprintf(statusfd, "exited %d\n", childinfo.si_status);
	    } else if (childinfo.si_code == CLD_KILLED) {
		dprintf(statusfd, "killed %d\n", childinfo.si_status);
	    } else if (childinfo.si_code == CLD_DUMPED) {
		dprintf(statusfd, "dumped %d\n", childinfo.si_status);
	    }
	}
    }
}

struct options {
    char *exec_file;
    char **exec_argv;
    int controlfd;
    int statusfd;
};

struct options
get_options(int argc, char **argv) {
    if (argc < 4) {
	warnx("Usage: %s controlfd statusfd command [args [args [args...]]]", (argv[0] ? argv[0] : "procfd"));
	exit(1);
    }
    const int controlfd = str_to_int(argv[1]);
    if (controlfd >= 0) {
	make_fd_cloexec_nonblock(controlfd);
    }
    const int statusfd = str_to_int(argv[2]);
    if (statusfd >= 0) {
	make_fd_cloexec_nonblock(statusfd);
    }
    const struct options opt = {
	.exec_file = argv[3],
	.exec_argv = argv+3,
	.controlfd = controlfd,
	.statusfd = statusfd,
    };
    return opt;
}

int main(int argc, char **argv) {
    disable_sigpipe();
    struct options opt = get_options(argc, argv);

    /* give filicide a trial run to see if we can do it;
     * it's idempotent, so no worries */
    filicide();
    void handle_exit(void) {
	filicide();
	dprintf(opt.statusfd, "terminating\n");
    };
    atexit(handle_exit);

    try_(prctl(PR_SET_CHILD_SUBREAPER, true));

    const sigset_t original_blocked_signals = get_blocked_signals();
    /* We use signalfds for signal handling. Among other benefits,
     * this means we don't need to worry about EINTR. */
    const int fatalfd = get_fatalfd();
    const int childfd = get_childfd();

    const pid_t main_child_pid = try_(fork());
    if (main_child_pid == 0) {
	/* the child will automatically get sigterm when the parent dies;
	 * a last-ditch effort to be effective even if we get SIGKILL'd */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	/* restore the signal mask */
	try_(sigprocmask(SIG_SETMASK, &original_blocked_signals, NULL));
	try_(execvp(opt.exec_file, opt.exec_argv));
    }

    dprintf(opt.statusfd, "pid %d\n", main_child_pid);

    struct pollfd pollfds[3] = {
	{ .fd = opt.controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
	{ .fd = fatalfd, .events = POLLIN, .revents = 0, },
    };
    for (;;) {
	try_(poll(pollfds, 3, -1));
	if (pollfds[0].revents & POLLIN) read_controlfd(opt.controlfd, main_child_pid);
	if (pollfds[0].revents & (POLLERR|POLLNVAL|POLLRDHUP|POLLHUP)) {
	    close(opt.controlfd);
	    opt.controlfd = -1;
	    pollfds[0].fd = -1;
	    /*
	       If we see our controlfd close, it means our process was wanted at some
	       time, but no longer. We will stick around until we have finished writing
	       status messages for killing all our children.
	    */
	    filicide();
	}
	if (pollfds[1].revents & POLLIN) {
	    read_childfd(childfd, opt.statusfd, main_child_pid);
	}
	if (pollfds[2].revents & POLLIN) {
	    read_fatalfd(fatalfd);
	}
	if ((pollfds[1].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[2].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll for signalfd");
	}
    }
}
