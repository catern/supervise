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
#include <syscall.h>
#include "subreap_lib.h"
#include "common.h"

/* This is at most PID_MAX_LIMIT, which is 2^22, approximately 4 million. */
pid_t get_maxpid(void) {
    int maxpidfd _cleanup_close_ = try_(open("/proc/sys/kernel/pid_max", O_RDONLY));
    char buf[64] = {};
    try_(read(maxpidfd, buf, sizeof(buf)));
    return str_to_int(buf);
}

/* Only available on some kernels. Generally available on >4.2. See proc(5).
 * It could be faster than iterating over all pids, though its interaction with
 * more children showing up mid-read is undefined.
 */
FILE *get_children_stream(pid_t pid) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "/proc/%d/task/%d/children", pid, pid);
    return fopen(buf, "re");
}

bool pid_exists(pid_t pid) {
    int ret = kill(pid, 0);
    return ret == 0 || (ret == -1 && errno == EPERM);
}

/* this returns -1 if there is no such pid (or if /proc/ is not available) */
pid_t ppid_of(pid_t pid) {
    /* Doing this pid_exists check is obviously racy, but it can only lead us to
     * erroneously return -1. This is the same effect as a process with this pid
     * being created after we have already done the open(/proc/pid/stat). And
     * this race is mitigated in the same way, through looping outside here.
     * And in return, we get a big boost to efficiency:
     * Checking pid_exists is much cheaper for the common case of the pid
     * not existing.
     */
    if (!pid_exists(pid)) return -1;
    char buf[BUFSIZ];
    snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
    const int statfd _cleanup_close_ = open(buf, O_CLOEXEC|O_RDONLY);
    if (statfd < 0) {
	if (errno == ENOENT || errno == ESRCH) return -1;
	err(1, "Failed to open(%s, O_CLOEXEC|O_RDONLY)", buf);
    }
    int ret = read(statfd, buf, sizeof(buf));
    if (ret < 0) {
	if (errno == ENOENT || errno == ESRCH) return -1;
	err(1, "Failed to read(<opened %s>, buf, sizeof(buf))", buf);
    }
    buf[ret] = '\0';
    /* the command string could have arbitrary characters in it, but
     * it ends with ')', so search for the last ')' in the buffer. */
    char *after_command = strrchr(buf, ')');
    if (after_command == NULL) {
	errx(1, "Failed to find ')' in %s", buf);
    }
    const int skip_to_ppid = 3;
    return str_to_int(after_command + skip_to_ppid);
}

/* this waits for a pid to die; it must be our child and must not have been
 * collected before; it leaves the zombie behind, uncollected. */
void wait_for_death(pid_t pid) {
    siginfo_t childinfo;
    try_(waitid(P_PID, pid, &childinfo, WEXITED|WNOWAIT));
}

/* returns true if it killed any children */
/* takes a bitset of dead child pids, and sets in it any new children killed */
bool kill_children(bool *dead, const pid_t max_pid, const pid_t mypid) {
    bool killed = false;
    /* this is pretty efficient because children have a higher pid than their
     * parents (modulo pid wraps), so iterating over all pids is equivalent to
     * just walking the tree. */
    /* pid wraps are dealt with by calling this function in a loop */
    for (pid_t pid = 1; pid < max_pid; pid++) {
	/* already killed */
	if (dead[pid]) continue;
	/* not our child or nonexistent */
	if (ppid_of(pid) != mypid) continue;
	/* this cannot fail. if we're here, this pid is an immediate child of
	 * us, and even if it already exited, it's still a zombie because we
	 * haven't collected it. */
	try_(kill(pid, SIGKILL));
	/* once pid is dead, all its children are reparented to us, and we will
	 * see them on the next iteration. but if we don't wait for it to die,
	 * we might exit before it gets SIGKILL and its children are reparented. */
	wait_for_death(pid);
	dead[pid] = true;
	killed = true;
    }
    return killed;
}

void kill_all_children(void) {
    const pid_t maxpid = get_maxpid();
    /* get my pid, bypassing glibc pid cache */
    const pid_t mypid = syscall(SYS_getpid);
    /* This is at most 4MB large, see PID_MAX_LIMIT and get_maxpid(). */
    bool dead[maxpid];
    memset(dead, false, sizeof(dead));
    /* keep killing children until there are no more to kill */
    while (kill_children(dead, maxpid, mypid));
    /* We will call kill_children at most max_pid times,
     * since we will kill each pid at most once. */
    /* (In the typical case we'll call it twice) */
}

void kill_children_up_to(const pid_t maxpid) {
    const pid_t mypid = syscall(SYS_getpid);
    bool dead[maxpid];
    memset(dead, false, sizeof(dead));
    while (kill_children(dead, maxpid, mypid));
}

/* On return, we guarantee that the current process has no more children. */
void filicide(void) {
    kill_all_children();
}

/* Exercise filicide to see if it works properly on this system.
 * We don't run real filicide, because it's fairly expensive.
*/
void trial_filicide(void) {
    kill_children_up_to(16);
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
