/* vi.c - You can't spell "evil" without "vi".
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/vi.html

USE_VI(NEWTOY(vi, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config VI
  bool "vi"
  default n
  help
    usage: vi FILE

    Visual text editor. Predates the existence of standardized cursor keys,
    so the controls are weird and historical.
*/

#define FOR_vi
#include "toys.h"

GLOBALS(
  struct linestack *ls;
  char *statline;
)

struct linestack_show {
  struct linestack_show *next;
  long top, left;
  int x, width, y, height;
};

// linestack, what to show, where to show it
void linestack_show(struct linestack *ls, struct linestack_show *lss)
{
  return;
}

void vi_main(void)
{
  int i;

  if (!(TT.ls = linestack_load(*toys.optargs)))
    TT.ls = xzalloc(sizeof(struct linestack));
 
  for (i=0; i<TT.ls->len; i++)
    printf("%.*s\n", (int)TT.ls->idx[i].len, (char *)TT.ls->idx[i].ptr);  
}
