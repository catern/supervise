#include "common.h"
#include <unistd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

int try_function(int ret, const char *file, int line, const char *function, const char *program)
{
    static int exiting = false;
    if (exiting == true) {
	warn("%s:%d %s: DOUBLE FAULT! Failed to %s", file, line, function, program);
	_exit(1);
    }
    if (ret < 0 && errno != EAGAIN) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	exiting = true;
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

void make_fd_cloexec_nonblock(int fd) {
    int fd_flags = try_(fcntl(fd, F_GETFD));
    try_(fcntl(fd, F_SETFD, fd_flags|FD_CLOEXEC));

    int fl_flags = try_(fcntl(fd, F_GETFL));
    try_(fcntl(fd, F_SETFL, fl_flags|O_NONBLOCK));
}
