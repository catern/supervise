#ifndef	_SUPERVISE_H
#define	_SUPERVISE_H	1

#include "supervise_protocol.h"

/* How do I cleanly express a large symbol? */
/* I can just export an array, meh. */
// figure out the size of the executable also
extern const char supervise_libexec[1];

/* does it have to be a separate process actually?
   can I set subreaper on an individual thread?

   probably not...
   and anyway, being in a separate process is nice fault isolation

   Hmm, I could maybe do it with __WNOTHREAD.

   Hmm. Yes.

   I'd need one thread per group-of-children.

   Urgh but it requires nothing in the whole program will wait for my children.

   I guess I need that anyway?

   And I can use a structured communication protocol since they'll be in the same process.

   Hmm, but who is the child actually reparented to?
   Hopefully the thread that is its ancestor...
   or is it just reparented to the "thread group"?
   Or just the first thread in the thread group?

   How will I do this in rsyscall?

   I'd need to load the library then make a stack and run the function on it.

   Jeese. That's pretty painful.

   OK, so it's a TODO to run supervise as a thread.

   Er wait, no, I wouldn't need to load the library and make a stack.

   I'd just need to make a new thread and I can directly run the syscalls needed.

   Er... that sounds slow.

   I guess if I just use the /proc/children approach...
   And just kill a bunch...

   Nah, I think having supervise is exactly one of the times
   when I'd want to separate out something into a standalone C program and run it directly on the remote box

   But it could still be done with threads, so maybe I could have a megaparent that does that.
   Er, no, I want to be able to directly create children and pass fds to them.

   Oh, and I can still have a thread-based implementation in Python, later on.
   I guess that would require an rsyscall library that can be run in a thread.

   OK so if I had a library-based, thread-based implementation.

   I'd essentially want to run the supervise loop on a new thread.
 */
// So, TODOs:
// - embed the executable in this library
// - try a thread-based implementation in Python

// Notes on a thread-based supervise:
/* so theoretically I could move my thread into the fd table context of the new thread, set up fds 0 and 1, and move back out. */
/* and in that case I wouldn't need any arguments whatsoever. in the worst case */
/* ohhh */
/* but how do I register atexit? */
/* OK, I would need to just call the exit syscall directly, wrapped with a filicide. */
/* And I'd need to have try_ do that. */
/* Same with errx. */
/* that means quite a redesign... */
/* oh, but wait a second. I definitely want/need the supervise thread to have its own fd table. */
/* so that when it exits, the fd is closed. */
/* Hmm, but then, how does signal handling work in the threaded case? */
/* and it would be neat if the thread would outlast the calling process. */
/* but in that case it needs its own address space, so I just want a process. so that's dumb. */
/* anyway, its own fd space, right. */
/* so that means performing the clone without CLONE_FILES, so it gets its own fd space copy */
/* and then I would, I guess, have some wrapper function which closes everything in the fd space except the controlfd and statusfd? */
/* and moves them to 0 and 1? */
/* easier to leave them where they are and pass them as arguments... */
/* and signal handling? well... */
/* if the surrounding process gets a signal, we die without a chance to handle. */
/* if it gets a signal and calls exit, we also die. */
/* basically it could do any kinda thing and kill us off, so pretty hard to handle this. */
#endif /* supervise.h */
