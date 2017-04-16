#include "common.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

int try_function(const int ret,
		 const char *file, const int line, const char *function, const char *program)
{
    /* EAGAIN and ECONNRESET are both detected through poll() for us */
    if (ret < 0 && errno != EAGAIN && errno != ECONNRESET) {
	warn("%s:%d %s: Failed to %s", file, line, function, program);
	exit(1);
    }
    return ret;
}

sigset_t singleton_set(const int signum) {
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

void exit_on_signal(int signal) {
    (void)signal;
    exit(1);
}

int get_childfd(void) {
    struct sigaction sa = {};
    sa.sa_handler = exit_on_signal;
    try_(sigaction(SIGCHLD, &sa, NULL));
    const sigset_t childsig = singleton_set(SIGCHLD);
    try_(sigprocmask(SIG_BLOCK, &childsig, NULL));
    const int childfd = try_(signalfd(-1, &childsig, SFD_NONBLOCK|SFD_CLOEXEC));
    return childfd;
}

void disable_sigpipe(void) {
    struct sigaction sa = {};
    sa.sa_handler = SIG_IGN;
    try_(sigaction(SIGPIPE, &sa, NULL));
}

void make_fd_cloexec_nonblock(const int fd) {
    const int fd_flags = try_(fcntl(fd, F_GETFD));
    try_(fcntl(fd, F_SETFD, fd_flags|FD_CLOEXEC));

    const int fl_flags = try_(fcntl(fd, F_GETFL));
    try_(fcntl(fd, F_SETFL, fl_flags|O_NONBLOCK));
}

int str_to_int(char const* str) {
    errno = 0;
    int ret = strtol(str, NULL, 10);
    if (errno != 0) {
	err(1, "strtol error on %s", str);
    }
    return ret;
}

void cleanup_close_func(int const* fdp) {
    close(*fdp);
}
void cleanup_fclose_func(FILE* const* filepp) {
    fclose(*filepp);
}
