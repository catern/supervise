from cffi import FFI
import pkgconfig

ffibuilder = FFI()
supervise = {key: list(value) for key, value in pkgconfig.parse('supervise').items()}

ffibuilder.set_source(
    "supervise_api._raw", """
#include "supervise.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
""", **supervise)

ffibuilder.cdef("""
typedef int... pid_t;
typedef unsigned... uid_t;
struct supervise_send_signal {
    pid_t pid;
    int signal;
};
typedef struct siginfo {
    int      si_code;      /* Signal code */
    pid_t    si_pid;       /* Sending process ID */
    uid_t    si_uid;       /* Real user ID of sending process */
    int      si_status;    /* Exit value or signal */
    ...;
} siginfo_t;
#define CLD_EXITED ... // child called _exit(2)
#define CLD_KILLED ... // child killed by signal
#define CLD_DUMPED ... // child killed by signal, and dumped core
#define CLD_STOPPED ... // child stopped by signal
#define CLD_TRAPPED ... // traced child has trapped
#define CLD_CONTINUED ... // child continued by SIGCONT
""")
