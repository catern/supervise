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
#include "common.h"
#include "subreap_lib.h"
#include "supervise_protocol.h"

bool called_filicide = false;

void filicide_once() {
    if (!called_filicide) {
	filicide();
        called_filicide = true;
    }
}

void handle_send_signal(struct supervise_send_signal signal) {
    /* we can only safely kill a pid if it's our child */
    if (waitid(P_PID, signal.pid, NULL, WNOWAIT) == 0) {
        kill(signal.pid, signal.signal);
    }
}

void read_controlfd(const int controlfd) {
    int size;
    struct supervise_send_signal signal;
    /* read a pid/signal pair to send */
    while ((size = try_(read(controlfd, &signal, sizeof(signal)))) > 0) {
	/* NOTE we assume we don't get partial reads. This is fine
         * since we're reading/writing in quantities less than
         * PIPE_BUF, so it's atomic. Nevertheless... */
        if (size != sizeof(signal)) {
            errx(1, "Inexplicable partial read from controlfd");
        }
	handle_send_signal(signal);
    }
}

void read_fatalfd(const int fatalfd) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(fatalfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	/* explicitly filicide, since dying from a signal won't call exit handlers */
        filicide_once();
	/* we will now exit in read_childfd when we see we have no children left */
    }
}

/* TODO ideally we would be able to write notifications for when
 * children are reparented to us. then we could signal them... but
 * maybe our owner already knows about our children through other
 * sources? so we still allow signaling everything. */
void read_childfd(int childfd, int statusfd) {
    struct signalfd_siginfo siginfo;
    /* signalfds can't have partial reads */
    while (try_(read(childfd, &siginfo, sizeof(siginfo))) == sizeof(siginfo)) {
	siginfo_t childinfo = {};
	for (;;) {
	    childinfo.si_pid = 0;
	    const int ret = waitid(P_ALL, 0, &childinfo, WEXITED|WNOHANG);
	    if (ret == -1 && errno == ECHILD) {
		exit(0);
	    }
	    /* no child was in a waitable state */
	    if (childinfo.si_pid == 0) break;
            size_t written = try_(write(statusfd, &childinfo, sizeof(childinfo)));
            /* We assume no partial writes since we're writing less
             * than PIPE_BUF, but nevertheless... */
            if (written != sizeof(childinfo)) {
                errx(1, "Inexplicable partial write on statusfd");
            }
        }
    }
}

int main() {
    const int controlfd = 0;
    const int statusfd = 1;
    supervise(opt.controlfd, opt.statusfd);
}

int supervise(const int controlfd, const int statusfd) {
    /* Check that this system is configured in such a way that we can
     * actually call filicide() and it will work. */
    sanity_check();
    atexit(filicide_once);

    /* We use signalfds for signal handling. Among other benefits,
     * this means we don't need to worry about EINTR. */
    const int fatalfd = get_fatalfd();
    const int childfd = get_childfd();

    struct pollfd pollfds[3] = {
	{ .fd = opt.controlfd, .events = POLLIN|POLLRDHUP, .revents = 0, },
	{ .fd = childfd, .events = POLLIN, .revents = 0, },
	{ .fd = fatalfd, .events = POLLIN, .revents = 0, },
    };
    for (;;) {
	try_(poll(pollfds, 3, -1));
	if (pollfds[0].revents & POLLIN) read_controlfd(opt.controlfd);
	if (pollfds[0].revents & (POLLERR|POLLNVAL|POLLRDHUP|POLLHUP)) {
	    close(opt.controlfd);
	    opt.controlfd = -1;
	    pollfds[0].fd = -1;
	    /*
	       If we see our controlfd close, it means our process was wanted at some
	       time, but no longer. We will stick around until we have finished writing
	       status messages for killing all our children.
	    */
            filicide_once();
	}
	if (pollfds[1].revents & POLLIN) {
	    read_childfd(childfd, opt.statusfd);
	}
	if (pollfds[2].revents & POLLIN) {
	    read_fatalfd(fatalfd);
	}
	if ((pollfds[1].revents & (POLLERR|POLLHUP|POLLNVAL)) ||
	    (pollfds[2].revents & (POLLERR|POLLHUP|POLLNVAL))) {
	    errx(1, "Error event returned by poll for signalfd");
	}
    }
}
