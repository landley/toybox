/* tac.c - output lines in reverse order
 *
 * Copyright 2012 Rob Landley <rob@landley.net>

USE_TAC(NEWTOY(tac, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config TAC
  bool "tac"
  default y
  help
    usage: tac [FILE...]

    Output lines in reverse order.
*/

#include "toys.h"

static void do_tac(int fd, char *name)
{
  struct arg_list *list = NULL;
  FILE *fp;

  if (fd == -1) {
    perror_msg_raw(name);
    return;
  }

  // Read in lines.
  fp = xfdopen(fd, "r");
  for (;;) {
    char *line = NULL;
    size_t allocated_length;

    if (getline(&line, &allocated_length, fp) <= 0) break;

    struct arg_list *temp = xmalloc(sizeof(struct arg_list));
    temp->next = list;
    temp->arg = line;
    list = temp;
  }
  fclose(fp);

  // Play them back.
  while (list) {
    struct arg_list *temp = list->next;
    xprintf("%s", list->arg);
    free(list->arg);
    free(list);
    list = temp;
  }
}

void tac_main(void)
{
  loopfiles_rw(toys.optargs, O_RDONLY, 0, do_tac);
}
