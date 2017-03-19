Summary
=======

This provides a superior API for process management and supervision on Linux,
in the form of a minimal and unprivileged utility "supervise".
supervise is designed to be used as a wrapper for child processes you execute.
Therefore supervise is designed to only wake up when there is an event to be handled,
and it otherwise consumes no CPU time.

There are four main benefits of using supervise to wrap child processes:

- The ability to get notified of child process exit or status change through a file descriptor interface
- The ability to send a signal to all the transitive children of a child process
- Automatic termination of your child processes when you exit (by virtue of the fd interface)
- Guaranteed termination of all the transitive children of a child
  process; no possibility of orphans lingering on the system

It is a fully supported design goal for supervise to be usable in a nested way.
You can wrap a child process with `supervise` which in turn forks off more child processes and wraps them in `supervise`,
in arbitrary configurations and depths.

Behavior
========

supervise `fork`s and `execvp`s a single immediate child process.
supervise then monitors two file descriptors passed in at startup,
the `controlfd` and the `statusfd`
(in most use cases these will be the same fd),
as well as waiting for the immediate child process to change state.

supervise reads `signal` or `signal_all` commands from `controlfd`,
which respectively command supervise to send a given signal to the immediate child process or to all of supervise's transitive children.

If supervise receives a POLLHUP on the `controlfd`, it will exit.
So if you fork off a child and wrap it with supervise,
that child and all its transitive children will automatically be cleaned up when you exit.

When the immediate child process changes state, such as by exiting,
supervise writes the state change to `statusfd`.

When supervise exits, it terminates all its transitive children.
It is not possible for transitive children of supervise to escape supervise's notice,
so when supervise exits it fully cleans up all processes that were created by it or its transitive children.
This is achieved through the use of CHILD_SUBREAPER Linux API.

Invocation and use
==================

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

controlfd commands
------------------

Write the following commands to `controlfd` to have the corresponding effect.
Follow all commands with a newline.

- `signal [signum]`: Send signal number `signum` to the immediate child
- `signal_all [signum]`: Send signal number `signum` to all transitive children

Furthermore, if supervise reads an EOF/POLLHUP from `controlfd`,
indicating there are no more fds open which can write to `controlfd`,
supervise will SIGKILL all its child processes.
(supervise will exit shortly thereafter after writing the status to statusfd.)

A binary interface will be supported soon.

statusfd updates
----------------

Read the following messages from `statusfd` to learn about the corresponding status changes.
All messages are followed by a newline.

- `pid [pid number]`: We have forked off the immediate child, and this is its pid.
- `exited [status code]`: The immediate child has exited
- `signaled [signal string]`: The immediate child has terminated due to a signal.
- `signaled [signal string] (coredumped)`: The immediate child has terminated due to a signal, and dumped core.
- `stopped [signal string]`: The immediate child has stopped due to a signal
- `continued`: The immediate child has continued due to SIGCONT.

A binary interface will be supported soon.

When does supervise exit?
=========================

To assure you of the correctness of supervise, know that supervise will exit in these four cases:

- All our children are dead.
- A control fd for communications was passed in, and has now closed.
  This indirectly causes us to exit; first we kill all of our children, and then exit because of that.
- We received a non-SIGKILL fatal signal that was not blocked or ignored when we started.
- Some believed-to-be-impossible syscall error happened.

In all of these cases, after we exit, all of our children will be dead.
(And our communication fds will be closed)

We will also exit in these two cases:

- We received SIGKILL
- A second believed-to-be-impossible syscall error happened during exiting

In these cases, some of our children may be able to leak.
Hopefully some day soon these holes can be removed by kernel support for an API like this.

TODO
====

- Support a binary interface in addition to the text interface
- Fix the text interface to cope with partial reads and multiple lines per read
- Optimization: support reading `/proc/[pid]/task/[tid]/children`
