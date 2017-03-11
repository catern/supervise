#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include "subreap_lib.h"
#include "common.h"

pid_t get_maxpid(void) {
    return 32768;
}

enum pid_state {
    /* we haven't checked this pid's parent yet */
    PID_UNCHECKED = 0,
    /* this pid is descended from us */
    PID_DESCENDED,
    /* this pid not descended from us */
    PID_UNRELATED,
};

/* Only available on some kernels. */
FILE *get_children_stream(pid_t pid) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "/proc/%d/task/%d/children", pid, pid);
    return fopen(buf, "re");
}

/* build a child tree, using the /proc/pid/task/tid/children file */
void build_child_tree_childrenfile(char *pid_state, pid_t max_pid) {
    memset(pid_state, PID_UNRELATED, max_pid);
    /* TOOD THIS IS WRONG WE NEED TO ITERATE OVER ALL TIDS */
    void mark_child(pid_t pid) {
	FILE *children = get_children_stream(pid);
	/* we assume that if we can't open its children file, this
	 * process doesn't exist */
	if (children == NULL) return;
	pid_state[pid] = PID_DESCENDED;
	pid_t child;
	while (fscanf(children, "%d", &child) > 0) {
	    mark_child(child);
	}
    }
    mark_child(getpid());
}

pid_t get_ppid_of(pid_t pid) {
    char buf[BUFSIZ];
    int statfd;
    snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
    if ((statfd = open(buf, O_CLOEXEC|O_RDONLY)) < 0) return -1;
    buf[try_(read(statfd, buf, sizeof(buf)))] = '\0';
    pid_t ppid = -1;
    sscanf(buf, "%*d (%*[^)]) %*c %d", &ppid);
    close(statfd);
    return ppid;
}

/* This is racy because a process could fork and create a new process
 * with a pid we already checked. However, we can solve the race by
 * calling this in a loop.
 */
void build_child_tree(char *pid_state, pid_t max_pid) {
    memset(pid_state, PID_UNCHECKED, max_pid);
    /* we are descended from ourselves */
    pid_state[getpid()] = PID_DESCENDED;
    /* init is not descended from us */
    pid_state[1] = PID_UNRELATED;
    /* neither is the kernel */
    pid_state[0] = PID_UNRELATED;
    char check(pid_t pid) {
	if (pid < 0) return PID_UNRELATED;
	if (pid_state[pid] == PID_UNCHECKED) {
	    /* a pid's descended-status is the same as its parent */
	    pid_t parent = get_ppid_of(pid);
	    pid_state[pid] = check(parent);
	}
	return pid_state[pid];
    }
    for (pid_t pid = 1; pid < max_pid; pid++) {
	check(pid);
    }
}

/* This signals each pid at most once, so it will halt. pid reuse
 * attacks are prevented by CHILD_SUBREAPER; we are choosing to not
 * collect children while in this function.  A malicious child (with
 * perfect timing) can at worst force us to loop max_pid times.
 */
void signal_all_children(int signum) {
    pid_t max_pid = get_maxpid();
    char pid_state[max_pid];

    bool already_signaled[max_pid];
    memset(already_signaled, false, max_pid);
    pid_t mypid = getpid();
    bool maybe_more_to_signal = true;
    /* while there might be some child left unsignaled: */
    while (maybe_more_to_signal) {
	/* build a tree of all children */
	build_child_tree(pid_state, max_pid);
	/* optimistically assume there won't be any children to signal this time */
	maybe_more_to_signal = false;
	/* signal all the children, skipping ones already signaled */
	for (pid_t pid = 1; pid < max_pid; pid++) {
	    if (pid_state[pid] == PID_DESCENDED && pid != mypid && !already_signaled[pid]) {
		fprintf(stderr, "killing %d with %d\n", pid, signum);
		kill(pid, signum);
		already_signaled[pid] = true;
		/* if we signaled something new, it could have forked and we missed its
		 * child when building the tree. so there's maybe more to signal */
		maybe_more_to_signal = true;
	    }
	}
    }
}

/* On return, we guarantee that the current process has no more children. */
void filicide() {
    signal_all_children(SIGKILL);
}

/* One other possible approach to filicide is to iterate over our
 * current children and kill them, and repeat until we don't see any
 * more current children. That would avoid iteration of proc. */

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
sigset_t fatalsig_set(void) {
    sigset_t already_blocked;
    try_(sigprocmask(0, NULL, &already_blocked));
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

int get_fatalfd(void) {
    sigset_t original_blocked_signals;
    sigset_t fatalsigs = fatalsig_set();
    try_(sigprocmask(SIG_BLOCK, &fatalsigs, &original_blocked_signals));
    int fatalfd = try_(signalfd(-1, &fatalsigs, SFD_NONBLOCK|SFD_CLOEXEC));
    return fatalfd;
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
	raise(siginfo.ssi_signo);
	/* exit just in case it doesn't kill us */
	fprintf(stderr, "signal %d doesn't seem to have killed us\n", siginfo.ssi_signo);
	exit(EXIT_FAILURE);
    }
}
