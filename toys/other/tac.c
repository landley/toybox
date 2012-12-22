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

void do_tac(int fd, char *name)
{
  struct arg_list *list = NULL;
  char *c;

  // Read in lines
  for (;;) {
    struct arg_list *temp;
    long len;

    if (!(c = get_rawline(fd, &len, '\n'))) break;

    temp = xmalloc(sizeof(struct arg_list));
    temp->next = list;
    temp->arg = c;
    list = temp;
  }

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
  loopfiles(toys.optargs, do_tac);
}
