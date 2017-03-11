/* 
a utility called procfd.

it accepts two fds named on the command line, controlfd and statusfd
it forks a single child and execvp's the rest of its arguments

it:
- waits to be able to read controlfd
- waits for the child to exit

if it is able to read a line from controlfd, and if that line matches "signal %d",
    it sends that signal to the child
if the fd closes,
    it sends SIGTERM to the child
if the child exits,
    it writes the exit status of the child to statusfd, and exits with the same code

furthermore, if this utility exits, the child automatically receives SIGTERM,
through PR_SET_PDEATHSIG

TODO: support writing and reading in binary
and switch between text and binary based on a flag

also this is vulnerable to pid wrap attacks
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

int try_function(int ret, const char *file, int line, const char *function, const char *program)
{
    if (ret < 0 && errno != EAGAIN) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	raise(SIGABRT);
	exit(EXIT_FAILURE);
    }
    return ret;
}

#define try_(x) \
    try_function(x, __FILE__, __LINE__, __FUNCTION__, #x )

void read_childfd(int childfd, int main_child_pid, int statusfd) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(childfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	siginfo_t childinfo;
	while (try_(waitid(P_ALL, 0, &childinfo, WEXITED|WNOHANG)) >= 0) {
	    if (childinfo.si_pid != main_child_pid) continue;

	    /* stringify wstatus and print it */
	    if (childinfo.si_code == CLD_EXITED) {
		dprintf(statusfd, "exited %d\n", childinfo.si_status);
	    } else if (childinfo.si_code == CLD_KILLED) {
		dprintf(statusfd, "signalled %s\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_DUMPED) {
		dprintf(statusfd, "signalled %s (coredumped)\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_STOPPED) {
		dprintf(statusfd, "stopped %s\n", strsignal(childinfo.si_status));
	    } else if (childinfo.si_code == CLD_CONTINUED) {
		dprintf(statusfd, "continued\n");
	    }

	    if (childinfo.si_code == CLD_EXITED) {
		exit(childinfo.si_status);
	    } else if (childinfo.si_code == CLD_KILLED || childinfo.si_code == CLD_DUMPED) {
		exit(EXIT_FAILURE);
	    }
	}
    }
}

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

sigset_t singleton_set(int signum) {
    sigset_t sigset;
    try_(sigemptyset(&sigset));
    try_(sigaddset(&sigset, signum));
    return sigset;
}
int main(int argc, char **argv) {
    { struct sigaction sa = {};
      sa.sa_handler = SIG_IGN;
      sigaction(SIGPIPE, &sa, NULL); };
    struct options opt = get_options(argc, argv);

    sigset_t original_blocked_signals;
    try_(sigprocmask(0, NULL, &original_blocked_signals));

    sigset_t childsig = singleton_set(SIGCHLD);
    try_(sigprocmask(SIG_BLOCK, &childsig, NULL));
    int childfd = try_(signalfd(-1, &childsig, SFD_NONBLOCK|SFD_CLOEXEC));

    pid_t main_child_pid = try_(fork());
    if (main_child_pid == 0) {
	/* the child will automatically get sigterm when the parent dies */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	/* restore the signal mask */
	try_(sigprocmask(SIG_SETMASK, &original_blocked_signals, NULL));
	try_(execvp(opt.exec_file, opt.exec_argv));
    }

    struct pollfd pollfds[2] = {
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
	{ .fd = opt.controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
    };
    for (;;) {
	try_(poll(pollfds, 2, -1));
	if (pollfds[0].revents & POLLIN) read_childfd(childfd, main_child_pid, opt.statusfd);
	if (pollfds[1].revents & POLLIN) read_controlfd(opt.controlfd, main_child_pid);
	if (pollfds[1].revents & (POLLRDHUP|POLLHUP)) {
	    try_(kill(main_child_pid, SIGTERM));
	    opt.controlfd = -1;
	}
	if ((pollfds[0].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[1].revents & (POLLERR|POLLNVAL))) {
	    errx(1, "Error event returned by poll");
	}
    }
}
