#pragma once
#include <sys/types.h>

struct supervise_send_signal {
    pid_t pid;
    int signal;
};

/* How do I cleanly express a large symbol? */
/* I can just export an array, meh. */
extern const char supervise_libexec[Figure out the size of the executable];

/* does it have to be a separate process actually?
   can I set subreaper on an individual thread?
 */
