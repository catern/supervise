#include "common.h"
#include <unistd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>

int try_function(int ret, const char *file, int line, const char *function, const char *program)
{
    if (ret < 0 && errno != EAGAIN) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	raise(SIGABRT);
	exit(1);
    }
    return ret;
}

sigset_t singleton_set(int signum) {
    sigset_t sigset;
    try_(sigemptyset(&sigset));
    try_(sigaddset(&sigset, signum));
    return sigset;
}

sigset_t get_blocked_signals() {
    sigset_t original_blocked_signals;
    try_(sigprocmask(SIG_BLOCK, NULL, &original_blocked_signals));
    return original_blocked_signals;
}

int get_childfd(void) {
    sigset_t childsig = singleton_set(SIGCHLD);
    try_(sigprocmask(SIG_BLOCK, &childsig, NULL));
    int childfd = try_(signalfd(-1, &childsig, SFD_NONBLOCK|SFD_CLOEXEC));
    return childfd;
}

void disable_sigpipe(void) {
    struct sigaction sa = {};
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

void read_childfd(int childfd, void (*handler)(siginfo_t)) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(childfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	siginfo_t childinfo;
	while (try_(waitid(P_ALL, 0, &childinfo, WEXITED|WNOHANG)) >= 0) {
	    handler(childinfo);
	}
    }
}
