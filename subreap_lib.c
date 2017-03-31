#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "subreap_lib.h"
#include "common.h"

/* This is at most PID_MAX_LIMIT, which is 2^22, approximately 4 million. */
pid_t get_maxpid(void) {
    int maxpidfd _cleanup_close_ = try_(open("/proc/sys/kernel/pid_max", O_RDONLY));
    char buf[64] = {};
    try_(read(maxpidfd, buf, sizeof(buf)));
    return str_to_int(buf);
}

enum pid_state {
    /* we haven't checked this pid's parent yet */
    PID_UNCHECKED = 0,
    /* this pid is descended from us */
    PID_DESCENDED,
    /* this pid not descended from us */
    PID_UNRELATED,
};

/* Only available on some kernels. Generally available on >4.2. See proc(5). */
FILE *get_children_stream(pid_t pid) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "/proc/%d/task/%d/children", pid, pid);
    return fopen(buf, "re");
}
/* Note that this allows us to avoid iterating over all pids, at least when
 * building the child tree. Some people are concerned that
 * build_child_tree_stat, which could have to process 4 million pids at worst,
 * is too slow. I am not really concerned about that - who cares if it's slow,
 * since it works? - but build_child_tree_children could be a solution for them
 * on modern kernels.
 */

/* Build a child tree, using the /proc/pid/task/tid/children
 * file. This should be faster than iterating over all pids, but it's
 * racy in the same way. */
void build_child_tree_children(char *pid_state, pid_t max_pid) {
    memset(pid_state, PID_UNRELATED, max_pid);
    /* TOOD this is wrong, we need to iterate over all tids */
    void mark_child(pid_t pid) {
	FILE *children _cleanup_fclose_ = get_children_stream(pid);
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

bool pid_exists(pid_t pid) {
    int ret = kill(pid, 0);
    return ret == 0 || (ret == -1 && errno == EPERM);
}

/* this returns -1 if there is no such pid (or if /proc/ is not available) */
pid_t get_ppid_of(pid_t pid) {
    /* Doing this pid_exists check is obviously racy, but it can only lead us to
     * erroneously return -1. This is the same effect as a process with this pid
     * being created after we have already done the open(/proc/pid/stat). So
     * this race is mitigated in the same way, through looping in
     * signal_all_children.  And in return, we get a big boost to efficiency:
     * Checking pid_exists is much cheaper for the common case of the pid
     * doesn't exist.
     */
    if (!pid_exists(pid)) return -1;
    char buf[BUFSIZ];
    snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
    const int statfd _cleanup_close_ = open(buf, O_CLOEXEC|O_RDONLY);
    if (statfd < 0) {
	if (errno == ENOENT) return -1;
	err(1, "Failed to open(%s, O_CLOEXEC|O_RDONLY)", buf);
    }
    buf[try_(read(statfd, buf, sizeof(buf)))] = '\0';
    /* the command string could have arbitrary characters in it, but
     * it ends with ')', so search for the last ')' in the buffer. */
    char *after_command = strrchr(buf, ')');
    if (after_command == NULL) {
	errx(1, "Failed to find ')' in %s", buf);
    }
    const int skip_to_ppid = 3;
    return str_to_int(after_command + skip_to_ppid);
}

/* This is racy because a process could fork and create a new process
 * with a pid we already checked. However, we can solve the race by
 * calling this in a loop.
 */
void build_child_tree_stat(char *pid_state, const pid_t max_pid) {
    memset(pid_state, PID_UNCHECKED, max_pid);
    /* we are descended from ourselves */
    pid_state[getpid()] = PID_DESCENDED;
    /* init is not descended from us */
    pid_state[1] = PID_UNRELATED;
    /* neither is the kernel */
    pid_state[0] = PID_UNRELATED;
    char check(const pid_t pid) {
	if (pid < 0) return PID_UNRELATED;
	if (pid_state[pid] == PID_UNCHECKED) {
	    /* a pid's descended-status is the same as its parent */
	    const pid_t parent = get_ppid_of(pid);
	    pid_state[pid] = check(parent);
	}
	return pid_state[pid];
    }
    for (pid_t pid = 1; pid < max_pid; pid++) {
	check(pid);
    }
}

bool check_for_stat() {
    /* if we can't look up our own ppid, /proc/pid/stat is unavailable */
    return get_ppid_of(getpid()) != -1;
}

/* check if various interfaces are available and use the fastest */
void build_child_tree(char *pid_state, const pid_t max_pid) {
    /* this is the only method we support for now */
    if (check_for_stat()) {
	return build_child_tree_stat(pid_state, max_pid);
    } else {
	errx(1, "No method to build_child_tree is available");
    }
}

/* This signals each pid at most once, so it will halt. pid reuse
 * attacks are prevented by CHILD_SUBREAPER; we are choosing to not
 * collect children while in this function.  A malicious child (with
 * perfect timing) can at worst force us to loop max_pid times.
 */
void signal_all_children(const int signum) {
    const pid_t max_pid = get_maxpid();
    const pid_t mypid = getpid();
    /* These are at most 4MB large each, see PID_MAX_LIMIT and get_maxpid(). */
    char pid_state[max_pid];
    bool already_signaled[max_pid];
    memset(already_signaled, false, max_pid);

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
		warnx("killing %d with %d", pid, signum);
		/* the kill can't fail. this pid must existed since it
		 * was marked as a descendent, and if it exited it's
		 * still a zombie */
		try_(kill(pid, signum));
		already_signaled[pid] = true;
		/* if we signaled something new, it could have forked and we missed its
		 * child when building the tree. so there's maybe more to signal */
		maybe_more_to_signal = true;
	    }
	}
    }
}

/* On return, we guarantee that the current process has no more children. */
void filicide(void) {
    signal_all_children(SIGKILL);
}
/* Another possible implementation of filicide is to iterate over our
 * current children and kill them, and repeat until we don't see any
 * more living children. That would avoid any tree-building, but it
 * can only work for sending SIGKILL. */

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

/* returns the set of fatal signals that are not blocked or ignored */
sigset_t fatalsig_set(void) {
    const sigset_t already_blocked = get_blocked_signals();
    sigset_t sigset;
    try_(sigemptyset(&sigset));
    for (int i = 0; i < (int)(sizeof(deathsigs)/sizeof(deathsigs[0])); i++) {
	const int signum = deathsigs[i];
	if (try_(sigismember(&already_blocked, signum))) continue;
	struct sigaction current;
	try_(sigaction(signum, NULL, &current));
	if (current.sa_handler == SIG_IGN) continue;
	try_(sigaddset(&sigset, signum));
    }
    return sigset;
}

int get_fatalfd(void) {
    const sigset_t fatalsigs = fatalsig_set();
    try_(sigprocmask(SIG_BLOCK, &fatalsigs, NULL));
    int fatalfd = try_(signalfd(-1, &fatalsigs, SFD_NONBLOCK|SFD_CLOEXEC));
    return fatalfd;
}
