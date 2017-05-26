/* tee.c - cat to multiple outputs.
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/tee.html

USE_TEE(NEWTOY(tee, "ia", TOYFLAG_USR|TOYFLAG_BIN))

config TEE
  bool "tee"
  default y
  help
    usage: tee [-ai] [file...]

    Copy stdin to each listed file, and also to stdout.
    Filename "-" is a synonym for stdout.

    -a	append to files
    -i	ignore SIGINT
*/

#define FOR_tee
#include "toys.h"

GLOBALS(
  void *outputs;
)

struct fd_list {
  struct fd_list *next;
  int fd;
};

// Open each output file, saving filehandles to a linked list.

static void do_tee_open(int fd, char *name)
{
  struct fd_list *temp;

  temp = xmalloc(sizeof(struct fd_list));
  temp->next = TT.outputs;
  temp->fd = fd;
  TT.outputs = temp;
}

void tee_main(void)
{
  if (toys.optflags & FLAG_i) xsignal(SIGINT, SIG_IGN);

  // Open output files
  loopfiles_rw(toys.optargs,
    O_RDWR|O_CREAT|WARN_ONLY|((toys.optflags & FLAG_a)?O_APPEND:O_TRUNC),
    0666, do_tee_open);

  for (;;) {
    struct fd_list *fdl;
    int len;

    // Read data from stdin
    len = xread(0, toybuf, sizeof(toybuf));
    if (len<1) break;

    // Write data to each output file, plus stdout.
    fdl = TT.outputs;
    for (;;) {
      if(len != writeall(fdl ? fdl->fd : 1, toybuf, len)) toys.exitval=1;
      if (!fdl) break;
      fdl = fdl->next;
    }
  }
}
