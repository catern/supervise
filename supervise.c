/* 
THE SUPERVISOR!!!!!!!
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
#include "common.h"
#include "subreap_lib.h"

/* screw it, I'm just going to assume that this is a single line */
/* it could actually be multiple lines, but meh */
void handle_control_message(int main_child_pid, char *str) {
    /* TODO sscanf is unsafe blargh */
    uint32_t signal = -1;
    if (sscanf(str, "signal %u\n", &signal) < 1) {
	err(1, "%s:%d %s: Failed to read(controlfd, &buf, sizeof(buf)-1)",
	    __FILE__, __LINE__, __FUNCTION__);
    }
    try_(kill(main_child_pid, signal));
}

void read_controlfd(int controlfd, int main_child_pid) {
    for (;;) {
	char buf[4096] = {};
	int ret = read(controlfd, &buf, sizeof(buf)-1);
	if ((ret == -1 && errno == EAGAIN) || ret == 0) return;
	if (ret < 0) {
	    err(1, "%s:%d %s: Failed to read(controlfd, &buf, sizeof(buf)-1)",
		__FILE__, __LINE__, __FUNCTION__);
	}
	buf[ret] = '\0';
	handle_control_message(main_child_pid, buf);
    }
}

void cloexec_nonblock_fd(int fd) {
    int fd_flags = try_(fcntl(fd, F_GETFD));
    try_(fcntl(fd, F_SETFD, fd_flags|FD_CLOEXEC));

    int fl_flags = try_(fcntl(fd, F_GETFL));
    try_(fcntl(fd, F_SETFL, fl_flags|O_NONBLOCK));
}

struct options {
    char *exec_file;
    char **exec_argv;
    int controlfd;
    int statusfd;
};

struct options
get_options(int argc, char **argv) {
    if (argc < 2) {
	fprintf(stderr, "Usage: %s controlfd statusfd [command]\n", (argv[0] ? argv[0] : "procfd"));
	exit(1);
    }
    errno = 0;
    int controlfd = strtol(argv[1], NULL, 0);
    if (errno != 0) {
	err(1, "strtol error on %s", argv[1]);
    }
    errno = 0;
    int statusfd = strtol(argv[2], NULL, 0);
    if (errno != 0) {
	err(1, "strtol error on %s", argv[2]);
    }
    cloexec_nonblock_fd(controlfd);
    cloexec_nonblock_fd(statusfd);
    struct options opt = {
	.exec_file = argv[3],
	.exec_argv = argv+3,
	.controlfd = controlfd,
	.statusfd = statusfd,
    };
    return opt;
}
int main(int argc, char **argv) {
    { struct sigaction sa = {};
      sa.sa_handler = SIG_IGN;
      sigaction(SIGPIPE, &sa, NULL); };
    struct options opt = get_options(argc, argv);

    /* give it a trial run to see if we can do it - it's idempotent, so no worries */
    filicide();
    atexit(filicide);

    try_(prctl(PR_SET_CHILD_SUBREAPER, true));

    sigset_t original_blocked_signals = get_blocked_signals();
    int fatalfd = get_fatalfd();
    int childfd = get_childfd();

    pid_t main_child_pid = try_(fork());
    if (main_child_pid == 0) {
	/* the child will automatically get sigterm when the parent dies */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	/* restore the signal mask */
	try_(sigprocmask(SIG_SETMASK, &original_blocked_signals, NULL));
	try_(execvp(opt.exec_file, opt.exec_argv));
    }

    struct pollfd pollfds[3] = {
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
	{ .fd = opt.controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
	{ .fd = fatalfd, .events = POLLIN, .revents = 0, },
    };
    void handle_child_status(siginfo_t childinfo) {
	if (childinfo.si_pid != main_child_pid) return;

	/* stringify wstatus and print it */
	if (childinfo.si_code == CLD_EXITED) {
	    dprintf(opt.statusfd, "exited %d\n", childinfo.si_status);
	} else if (childinfo.si_code == CLD_KILLED) {
	    dprintf(opt.statusfd, "signalled %s\n", strsignal(childinfo.si_status));
	} else if (childinfo.si_code == CLD_DUMPED) {
	    dprintf(opt.statusfd, "signalled %s (coredumped)\n", strsignal(childinfo.si_status));
	} else if (childinfo.si_code == CLD_STOPPED) {
	    dprintf(opt.statusfd, "stopped %s\n", strsignal(childinfo.si_status));
	} else if (childinfo.si_code == CLD_CONTINUED) {
	    dprintf(opt.statusfd, "continued\n");
	}
    }
    for (;;) {
	try_(poll(pollfds, 3, -1));
	if (pollfds[0].revents & POLLIN) read_childfd(childfd, handle_child_status);
	if (pollfds[1].revents & POLLIN) read_controlfd(opt.controlfd, main_child_pid);
	if (pollfds[1].revents & (POLLRDHUP|POLLHUP)) {
	    try_(kill(main_child_pid, SIGTERM));
	    opt.controlfd = -1;
	}
	if (pollfds[2].revents & POLLIN) read_fatalfd(fatalfd);
	if ((pollfds[0].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[1].revents & (POLLERR|POLLNVAL)) ||
	    (pollfds[2].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll");
	}
    }
}
