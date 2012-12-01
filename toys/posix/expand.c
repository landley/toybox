/* expand.c - expands tabs to space
 *
 * Copyright 2012 Jonathan Clairembault <jonathan at clairembault dot fr>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/expand.html

USE_EXPAND(NEWTOY(expand, "t*", TOYFLAG_USR|TOYFLAG_BIN))

config EXPAND
  bool "expand"
  default y
  help
    usage: expand [-t tablist] [file...]

    Expand tabs to spaces according to tabstops.

    -t	tablist

      Specify tab stops, either a single number instead of the default 8,
      or a comma separated list of increasing numbers representing tabstop
      positions (absolute, not increments) with each additional tab beyound
      that becoming one space.
*/

#define FOR_expand
#include "toys.h"

GLOBALS(
  struct arg_list *tabs;

  unsigned tabcount, *tab;
)

static void expand_file(int fd, char *name)
{
  int i, len, x=0, stop = 0;

  for (;;) {
    len = read(fd, toybuf, sizeof(toybuf));
    if (len<0) {
      perror_msg("%s", name);
      toys.exitval = 1;
      return;
    }
    if (!len) break;
    for (i=0; i<len; i++) {
      int len = 1;
      char c = toybuf[i];

      if (c != '\t') {
        if (EOF == putc(c, stdout)) perror_exit(0);

        if (c == '\b' && x) len = -1;
        if (c == '\n') {
          x = stop = 0;
          continue;
        }
      } else {
        if (TT.tabcount < 2) {
          len = TT.tabcount ? *TT.tab : 8;
          len -= x%len;
        } else while (stop < TT.tabcount) {
          if (TT.tab[stop] > x) {
            len = TT.tab[stop] - x;
            break;
          } else stop++;
        }
        xprintf("%*c", len, ' ');
      }
      x += len;
    }
  }
}

// Parse -t options to fill out unsigned array in tablist (if not NULL)
// return number of entries in tablist
static int parse_tablist(unsigned *tablist)
{
  struct arg_list *tabs;
  int tabcount = 0;

  for (tabs = TT.tabs; tabs; tabs = tabs->next) {
    char *s = tabs->arg;

    while (*s) {
      int count;
      unsigned x, *t = tablist ? tablist+tabcount : &x;

      if (tabcount >= sizeof(toybuf)/sizeof(unsigned)) break;
      if (sscanf(s, "%u%n", t, &count) != 1) break;
      if (tabcount++ && tablist && *(t-1) >= *t) break;
      s += count;
      if (*s==' ' || *s==',') s++;
      else break;
    }
    if (*s) error_exit("bad tablist");
  }

  return tabcount;
}

void expand_main(void)
{
  TT.tabcount = parse_tablist(NULL);

  // Determine size of tablist, allocate memory, fill out tablist
  if (TT.tabcount) {
    TT.tab = xmalloc(sizeof(unsigned)*TT.tabcount);
    parse_tablist(TT.tab);
  }

  loopfiles(toys.optargs, expand_file);
  if (CFG_TOYBOX_FREE) free(TT.tab);
}
