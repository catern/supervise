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

int try_function(int ret, const char *file, int line, const char *function, const char *program)
{
    if (ret < 0 && errno != EAGAIN) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	raise(SIGABRT);
	exit(1);
    }
    return ret;
}

#define try_(x) \
    try_function(x, __FILE__, __LINE__, __FUNCTION__, #x )

struct options {
    char *exec_file;
    char **exec_argv;
};

struct options
get_options(int argc, char **argv) {
    if (argc < 2) {
	fprintf(stderr, "Usage: %s [command]", (argv[0] ? argv[0] : "subreap"));
	exit(EXIT_FAILURE);
    }
    struct options opt = {
	.exec_file = argv[1],
	.exec_argv = argv+1,
    };
    return opt;
}

int get_children_fd() {
    char buf[1024];
    pid_t pid = getpid();
    try_(snprintf(buf, sizeof(buf), "/proc/%d/task/%d/children", pid, pid));
    return try_(open(buf, O_CLOEXEC|O_RDONLY));
}
/* TODO we need to support using ppid instead, ugh.
   probably through popen?
 */
/* dump 4096 bytes worth of children,
   kill all of them,
   then do it again. */
void filicide() {
    int children_fd = get_children_fd();
    char buf[4096];
    int size;
    for (;;) {
	try_(lseek(children_fd, 0, SEEK_SET));
	size = try_(read(children_fd, buf, sizeof(buf)-1));
	if (size == 0) return;
	buf[size] = '\0';
	char *child = strtok(buf, " ");
	do {
	    int childpid = strtol(child, NULL, 0);
	    printf("killing child: %d\n", childpid);
	    kill(childpid, SIGKILL);
	    /* since it was our child, we must be able to wait on it */
	    try_(waitpid(childpid, NULL, 0));
	} while ((child = strtok(NULL, " ")));
    }
}

const int deathsigs[] = {
    /* signals making us terminate */
    SIGHUP,
    SIGINT,
    SIGKILL,
    SIGPIPE,
    SIGALRM,
    SIGTERM,
    SIGUSR1,
    SIGUSR2,
    SIGPOLL,
    SIGPROF,
    SIGVTALRM,
    SIGIO,
    SIGPWR,
    /* signals making us coredump */
    SIGQUIT,
    SIGILL,
    SIGABRT,
    SIGFPE,
    SIGSEGV,
    SIGBUS,
    SIGSYS,
    SIGTRAP,
    SIGXCPU,
    SIGXFSZ,
};
/* returns the set of signals that are not blocked or ignored */
sigset_t fatalsig_set(sigset_t already_blocked) {
    sigset_t sigset;
    try_(sigemptyset(&sigset));
    for (int i = 0; i < (int)(sizeof(deathsigs)/sizeof(deathsigs[0])); i++) {
	int signum = deathsigs[i];
	if (try_(sigismember(&already_blocked, signum))) continue;
	struct sigaction current;
	try_(sigaction(signum, NULL, &current));
	if (current.sa_handler == SIG_IGN) continue;
	try_(sigaddset(&sigset, signum));
    }
    return sigset;
}
sigset_t singleton_set(int signum) {
    sigset_t sigset;
    try_(sigemptyset(&sigset));
    try_(sigaddset(&sigset, signum));
    return sigset;
}

void read_fatalfd(int fatalfd) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(fatalfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	/* explicitly filicide, since dying from a signal won't call exit handlers */
	filicide();
	/* allow the signal to be delivered and kill us */
	sigset_t singleton = singleton_set(siginfo.ssi_signo);
	try_(sigprocmask(SIG_UNBLOCK, &singleton, NULL));
	/* exit just in case it doesn't kill us */
	exit(EXIT_FAILURE);
    }
}
void read_childfd(int childfd, int main_child_pid) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(childfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	siginfo_t childinfo;
	while (try_(waitid(P_ALL, 0, &childinfo, WEXITED|WNOHANG)) >= 0) {
	    if (childinfo.si_pid != main_child_pid) continue;

	    if (childinfo.si_code == CLD_EXITED) {
		exit(childinfo.si_status);
	    } else if (childinfo.si_code == CLD_KILLED || childinfo.si_code == CLD_DUMPED) {
		exit(EXIT_FAILURE);
	    }
	}
    }
}

/* TODO
   Maybe I should run subreaper in front of procfd?
   maybe I should combine them into one program after all


   perhaps  I need to double-fork and set a controlling tty for killing hacks

   I can send TERM to all children, yield, then wait on all children,
   for a reasonable attempt at killing
   though that won't work really - I won't kill my grandchildren which frequently exist
   maybe just two levels then?

   consider modularizing subreaper, and procfd, and another setsid-ish group-signalling module,
   using the epoll method

   then we can easily compose them into one process
 */
int main(int argc, char **argv) {
    struct options opt = get_options(argc, argv);

    /* give it a trial run to see if we can do it - it's idempotent, so no worries */
    filicide();
    atexit(filicide);

    try_(prctl(PR_SET_CHILD_SUBREAPER, true));

    sigset_t original_blocked_signals;
    try_(sigprocmask(0, NULL, &original_blocked_signals));

    sigset_t fatalsigs = fatalsig_set(original_blocked_signals);
    try_(sigprocmask(SIG_BLOCK, &fatalsigs, NULL));
    int fatalfd = try_(signalfd(-1, &fatalsigs, SFD_NONBLOCK|SFD_CLOEXEC));

    sigset_t childsig = singleton_set(SIGCHLD);
    try_(sigprocmask(SIG_BLOCK, &childsig, NULL));
    int childfd = try_(signalfd(-1, &childsig, SFD_NONBLOCK|SFD_CLOEXEC));

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
    for (;;) {
	try_(poll(pollfds, 2, -1));
	if (pollfds[0].revents & POLLIN) read_fatalfd(fatalfd);
	if (pollfds[1].revents & POLLIN) read_childfd(childfd, main_child_pid);
	if ((pollfds[0].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[1].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll");
	}
    }
}
