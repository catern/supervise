This provides a superior API for process management on Linux,
in the form of a small utility "supervise".

There are three main features:

- The ability to monitor a child process's status through a file descriptor interface
- The ability to send a signal to all transitive children of a process through a file descriptor interface
- Guaranteed cleanup of a child process and all its transitive children

Supervise is invoked like this:

    supervise controlfd statusfd command [arg [arg...]]]

`controlfd` is the file descriptor number which supervise will read for control messages.

`statusfd` is the file descriptor number on which supervise will write status messages.

`command` and `arg`s are the command line which supervise will fork, execvp, and monitor.

supervise can be used from the shell, but it is especially useful when used from programming languages.
When used from programming languages, it should probably be wrapped in an interface looking something like this:

    spawnfd : string -> file_descriptor

which is implemented something like this:

	file_descriptor spawnfd(string command_line) {
        fdA, fdB = fdpair();
        command = "supervise ${fdA} ${fdA} ${command_line}";
        spawn(command);
	    close(fdA);
        return fdB;
	}

To signal the immediate child, write `signal [signum]`.
To signal all children, write `signal_all [signum]`.
To learn about status changes in the immediate child,
monitor the file descriptor for readability,
and read out messages of the form `exited [status code]` etc.

To assure you of the correctness of supervise, know that supervise will exit in these four cases:

- Control and/or status fds for communication were passed in, and they have all closed.
- All our children are dead.
- We received a non-SIGKILL fatal signal that was not blocked or ignored when we started.
- Some believed-to-be-impossible syscall error happened

In all of these cases, after we exit, all of our children will be dead.
(And our communication fds will be closed)

We will also exit in these two cases:

- We received SIGKILL
- A second believed-to-be-impossible syscall error happened during exiting

In these cases, some of our children may be able to leak.
Hopefully some day soon these holes can be removed by kernel support for an API like this.
