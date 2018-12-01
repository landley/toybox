/* portability.c - code to workaround the deficiencies of various platforms.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 * Copyright 2012 Georgi Chorbadzhiyski <gf@unixsol.org>
 */

#include "toys.h"

// We can't fork() on nommu systems, and vfork() requires an exec() or exit()
// before resuming the parent (because they share a heap until then). And no,
// we can't implement our own clone() call that does the equivalent of fork()
// because nommu heaps use physical addresses so if we copy the heap all our
// pointers are wrong. (You need an mmu in order to map two heaps to the same
// address range without interfering with each other.) In the absence of
// a portable way to tell malloc() to start a new heap without freeing the old
// one, you pretty much need the exec().)

// So we exec ourselves (via /proc/self/exe, if anybody knows a way to
// re-exec self without depending on the filesystem, I'm all ears),
// and use the arguments to signal reentry.

#if CFG_TOYBOX_FORK
pid_t xfork(void)
{
  pid_t pid = fork();

  if (pid < 0) perror_exit("fork");

  return pid;
}
#endif

void xgetrandom(void *buf, unsigned buflen, unsigned flags)
{
  int fd;

#if CFG_TOYBOX_GETRANDOM
  if (buflen == getrandom(buf, buflen, flags)) return;
  if (!CFG_TOYBOX_ON_ANDROID || errno!=ENOSYS) perror_exit("getrandom");
#endif

  fd = xopen(flags ? "/dev/random" : "/dev/urandom", O_RDONLY);
  xreadall(fd, buf, buflen);
  close(fd);
}

#if defined(__APPLE__)
extern char **environ;

int clearenv(void)
{
  *environ = NULL;
  return 0;
}
#endif
