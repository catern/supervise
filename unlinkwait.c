/*
 * Wait until the passed-in path has a link count of 0, then exit.
 *
 * This program is useful for one specific purpose:
 * bridging between filesystem refcounting and fd refcounting.
 * We can pass in a writable fd to a fifo to this program,
 * and call the program with the fifo's path as an argument (or /proc/self/fd/n).
 * Then as long as there is that fifo is accessible through the filesystem,
 * a writable fd will be kept open to the pipe.
 * That means we can rely on EOF/POLLHUP on read side of that fd,
 * to tell us when there are no more references possible to that fd,
 * and we can deallocate the resources it corresponds to.
 * The same trick would work with a Unix socket,
 * and possibly other things other than a fifo.
 */
#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>

#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include "common.h"

struct options {
    char *path;
};

struct options get_options(int argc, char **argv) {
    if (argc != 2) {
	warnx("Usage: %s path", (argv[0] ? argv[0] : "unlinkwait"));
	exit(1);
    }
    const struct options opt = {
	.path = argv[1],
    };
    return opt;
}

/* return an (inotify) fd which is readable when the link count */
int get_linkfd(int fd) {
    int ret = try_(inotify_init1(IN_CLOEXEC));
    char buf[1024];
    sprintf(buf, "/proc/self/fd/%d", fd);
    try_(inotify_add_watch(ret, buf, IN_ATTRIB));
    return ret;
}

bool has_links(int fd) {
    struct stat stat;
    try_(fstat(fd, &stat));
    return stat.st_nlink != 0;
}

void check_links(int fd) {
    if (!has_links(fd)) {
	exit(0);
    }
}

int main(int argc, char **argv) {
    struct options opt = get_options(argc, argv);

    int fd = try_(open(opt.path, O_RDONLY));
    int linkfd = get_linkfd(fd);

    /* do an initial check after opening the linkfd to avoid races */
    check_links(fd);
    for (;;) {
	union {
	    struct inotify_event event;
	    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	} in_ev;
	try_(read(linkfd, in_ev.buf, sizeof(in_ev.buf)));
	if (in_ev.event.mask & IN_ATTRIB) {
	    check_links(fd);
	}
	/* if we stop being able to watch for some reason, we have to exit */
	if (in_ev.event.mask & IN_IGNORED) {
	    /* maybe the event caused the link count to go to 0? */
	    check_links(fd);
	    exit(1);
	}
    };
}
