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
    } else if (sscanf(command, "signal_all %u\n", &signal) == 1) {
	signal_all_children(signal);
    }
}

void read_controlfd(const int controlfd, const int main_child_pid) {
    int size;
    char buf[4096] = {};
    while ((size = try_(read(controlfd, &buf, sizeof(buf)-1))) > 0) {
	buf[size] = '\0';
	/* BUG we assume we get full lines, one line at a time */
	/* (that's not as dangerous as it might seem due to pipe atomicity) */
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
	/* allow the signal to be delivered and kill us */
	const sigset_t singleton = singleton_set(siginfo.ssi_signo);
	try_(sigprocmask(SIG_UNBLOCK, &singleton, NULL));
	raise(siginfo.ssi_signo);
	/* exit just in case it doesn't kill us */
	warnx("signal %d doesn't seem to have killed us", siginfo.ssi_signo);
	exit(EXIT_FAILURE);
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
		dprintf(statusfd, "signaled %s\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_DUMPED) {
		dprintf(statusfd, "signaled %s (coredumped)\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_STOPPED) {
		dprintf(statusfd, "stopped %s\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_CONTINUED) {
		dprintf(statusfd, "continued\n");
	    }
	}
    }
}

struct options {
    char *exec_file;
    char **exec_argv;
    int controlfd;
    int statusfd;
    bool should_hang;
};

struct options
get_options(int argc, char **argv) {
    if (argc < 2) {
	warnx("Usage: %s controlfd statusfd [command]", (argv[0] ? argv[0] : "procfd"));
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
	/* if we don't have a statusfd or controlfd, we should just hang
	 * around until we get a signal, to be useful on the command line. */
	.should_hang = opt.statusfd == -1 && opt.controlfd == -1,
    };
    return opt;
}

int main(int argc, char **argv) {
    disable_sigpipe();
    struct options opt = get_options(argc, argv);

    /* give filicide a trial run to see if we can do it;
     * it's idempotent, so no worries */
    filicide();
    atexit(filicide);

    try_(prctl(PR_SET_CHILD_SUBREAPER, true));

    const sigset_t original_blocked_signals = get_blocked_signals();
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

    struct pollfd pollfds[4] = {
	{ .fd = opt.controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
	{ .fd = opt.statusfd,  .events = POLLRDHUP, .revents = 0, },
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
	{ .fd = fatalfd, .events = POLLIN, .revents = 0, },
    };
    for (;;) {
	if (opt.controlfd == -1 && opt.statusfd == -1 && !opt.should_hang) {
	    /* We exit if our means of communication with our owner have closed. */
	    exit(0);
	}
	try_(poll(pollfds, 4, -1));
	if (pollfds[0].revents & POLLIN) read_controlfd(opt.controlfd, main_child_pid);
	if (pollfds[0].revents & (POLLERR|POLLNVAL|POLLRDHUP|POLLHUP)) {
	    close(opt.controlfd);
	    opt.controlfd = -1;
	    pollfds[0].fd = -1;
	}
	if (pollfds[1].revents & (POLLERR|POLLNVAL|POLLRDHUP|POLLHUP)) {
	    close(opt.statusfd);
	    opt.statusfd = -1;
	    pollfds[1].fd = -1;
	}
	if (pollfds[2].revents & POLLIN) {
	    read_childfd(childfd, opt.statusfd, main_child_pid);
	}
	if (pollfds[3].revents & POLLIN) {
	    read_fatalfd(fatalfd);
	}
	if ((pollfds[2].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[3].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll for signalfd");
	}
    }
}
