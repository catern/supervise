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
    if (ret < 0) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	raise(SIGABRT);
	exit(1);
    }
    return ret;
}

#define try_(x) \
    try_function(x, __FILE__, __LINE__, __FUNCTION__, #x )

/* the pid of our child */
int childpid;
/* we read signals to send from here */
int controlfd;
/* we write child state changes here */
int statusfd;
/* we read sigchld siginfos from here*/
int sigchldfd;

/* a SIGCHLD doesn't necessarily mean we will exit - the child may
 * have just changed status. so we call this in a loop, and return
 * true iff we were able to wait() on something */
bool handle_sigchld() {
    int wstatus;
    /* TODO should we check for ECHLD and not exit? */
    if (try_(waitpid(childpid, &wstatus, WNOHANG|WUNTRACED|WCONTINUED)) == 0) return false;
    /* stringify wstatus and print it */
    if (WIFEXITED(wstatus)) {
	dprintf(statusfd, "exited %d\n", WEXITSTATUS(wstatus));
    } else if (WIFSIGNALED(wstatus)) {
	if (WCOREDUMP(wstatus)) {
	    dprintf(statusfd, "signalled %s (coredumped)\n", strsignal(WTERMSIG(wstatus)));
	} else {
	    dprintf(statusfd, "signalled %s\n", strsignal(WTERMSIG(wstatus)));
	}
    } else if (WIFSTOPPED(wstatus)) {
	    dprintf(statusfd, "stopped %s\n", strsignal(WSTOPSIG(wstatus)));
    } else if (WIFCONTINUED(wstatus)) {
	    dprintf(statusfd, "continued\n");
    }

    if (WIFEXITED(wstatus)) {
    	exit(WEXITSTATUS(wstatus));
    } else if (WIFSIGNALED(wstatus)) {
    	exit(1);
    }
    return true;
}

void read_sigchldfd() {
    for (;;) {
	struct signalfd_siginfo siginfo;
	int ret = read(sigchldfd, &siginfo, sizeof(siginfo));
	if (ret < 0) {
	    if (errno == EAGAIN) return;
	    err(1, "%s:%d %s: Failed to read(sigchldfd, &siginfo, sizeof(siginfo))",
		__FILE__, __LINE__, __FUNCTION__);
	}
	while (handle_sigchld());
    }
}

/* screw it, I'm just going to assume that this is a single line */
/* it could actually be multiple lines, but meh */
void handle_control_message(char *str) {
    /* TODO sscanf is unsafe blargh */
    uint32_t signal = -1;
    if (sscanf(str, "signal %u\n", &signal) < 1) {
	err(1, "%s:%d %s: Failed to read(controlfd, &buf, sizeof(buf)-1)",
	    __FILE__, __LINE__, __FUNCTION__);
    }
    try_(kill(childpid, signal));
}

void read_controlfd() {
    for (;;) {
	char buf[4096] = {};
	int ret = read(controlfd, &buf, sizeof(buf)-1);
	if ((ret == -1 && errno == EAGAIN) || ret == 0) return;
	if (ret < 0) {
	    if (errno == EAGAIN) return;
	    err(1, "%s:%d %s: Failed to read(controlfd, &buf, sizeof(buf)-1)",
		__FILE__, __LINE__, __FUNCTION__);
	}
	buf[ret] = '\0';
	handle_control_message(buf);
    }
}

void do_poll(int timeout) {
    struct pollfd pollfds[2] = {
	{ .fd = sigchldfd, .events = POLLIN, .revents = 0, },
	{ .fd = controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
    };
    if (try_(poll(pollfds, 2, timeout)) == 0) {
	return;
    }
    short sigchldfd_events = pollfds[0].revents;
    if (sigchldfd_events & (POLLERR|POLLHUP|POLLNVAL)) {
	errx(1, "%s:%d %s: Error event %x returned for sigchldfd",
	     __FILE__, __LINE__, __FUNCTION__, sigchldfd_events);
    }
    if (sigchldfd_events & POLLIN) {
	read_sigchldfd();
    }
    short controlfd_events = pollfds[1].revents;
    if (controlfd_events & (POLLERR|POLLNVAL)) {
	errx(1, "%s:%d %s: Error event %x returned for controlfd",
	     __FILE__, __LINE__, __FUNCTION__, controlfd_events);
    }
    if (controlfd_events & POLLIN) {
	read_controlfd();
    }
    if (controlfd_events & (POLLRDHUP|POLLHUP)) {
	try_(kill(childpid, SIGTERM));
	controlfd = -1;
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
    {   struct sigaction sa = {};
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL); };
    struct options opt = get_options(argc, argv);
    statusfd = opt.statusfd;
    controlfd = opt.controlfd;
    signal(SIGPIPE, SIG_IGN);

    sigset_t original_blocked_signals;
    sigset_t sigchld_set = singleton_set(SIGCHLD);
    try_(sigprocmask(SIG_BLOCK, &sigchld_set, &original_blocked_signals));
    sigchldfd = try_(signalfd(-1, &sigchld_set, SFD_NONBLOCK|SFD_CLOEXEC));

    do_poll(0);

    childpid = try_(fork());
    if (childpid == 0) {
	/* the child will automatically get sigterm when the parent dies */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	/* restore the signal mask */
	try_(sigprocmask(SIG_SETMASK, &original_blocked_signals, NULL));
	try_(execvp(opt.exec_file, opt.exec_argv));
    }
    for (;;) do_poll(-1);
}
