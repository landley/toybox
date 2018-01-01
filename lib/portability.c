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

#if defined(__APPLE__)
ssize_t getdelim(char **linep, size_t *np, int delim, FILE *stream)
{
  int ch;
  size_t new_len;
  ssize_t i = 0;
  char *line, *new_line;

  // Invalid input
  if (!linep || !np) {
    errno = EINVAL;
    return -1;
  }

  if (*linep == NULL || *np == 0) {
    *np = 1024;
    *linep = calloc(1, *np);
    if (*linep == NULL) return -1;
  }
  line = *linep;

  while ((ch = getc(stream)) != EOF) {
    if (i > *np) {
      // Need more space
      new_len = *np + 1024;
      new_line = realloc(*linep, new_len);
      if (!new_line) return -1;
      *np = new_len;
      line = *linep = new_line;
    }

    line[i++] = ch;
    if (ch == delim) break;
  }

  if (i > *np) {
    // Need more space
    new_len  = i + 2;
    new_line = realloc(*linep, new_len);
    if (!new_line) return -1;
    *np = new_len;
    line = *linep = new_line;
  }
  line[i] = '\0';

  return i > 0 ? i : -1;
}

ssize_t getline(char **linep, size_t *np, FILE *stream)
{
  return getdelim(linep, np, '\n', stream);
}

extern char **environ;

int clearenv(void)
{
  *environ = NULL;
  return 0;
}
#endif
