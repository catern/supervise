#ifndef	_SUPERVISE_PROTOCOL_H
#define	_SUPERVISE_PROTOCOL_H	1
#include <sys/types.h>

/* Send supervise_send_signal on the controlfd */
struct supervise_send_signal {
    pid_t pid;
    int signal;
};

/* Receive siginfo_t (defined in signal.h) on the statusfd for every
 * child status change. */

#endif /* supervise_protocol.h */
