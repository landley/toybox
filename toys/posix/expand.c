/* expand.c - expands tabs to space
 *
 * FIXME: handle backspace.
 *
 * Copyright 2012 Jonathan Clairembault <jonathan at clairembault dot fr>
 *
 * See http://http://pubs.opengroup.org/onlinepubs/9699919799/nframe.html

USE_EXPAND(NEWTOY(expand, "t:", TOYFLAG_USR|TOYFLAG_BIN))

config EXPAND
  bool "expand"
  default n
  help
    usage: expand [-t tablist] [file...]

    Command expand. Expands tabs to space according to tabstops.

    -t  tablist
    Specify the tab stops.  The argument tablist consists of either a single 
    strictly positive decimal integer or a list of tabstops. If a single number 
    is given, tabs are set that number of column positions apart instead of the 
    default 8.

    If a list of tabstops is given, the list is made of two or more strictly 
    positive decimal integers, separated by <blank> or <comma> characters, in 
    strictly ascending order. The <tab> characters are set at those specific 
    column positions.

    In the event of expand having to process a <tab> at a position beyond the 
    last of those specified in a multiple tab-stop list, the <tab> is replaced 
    by a single <space> in the output.

    Any <backspace> characters shall be copied to the output and cause the 
    column position count for tab stop calculations to be decremented; the 
    column position count shall not be decremented below zero.
*/

#define FOR_expand
#include "toys.h"

GLOBALS(
  char *t_flags;
  struct offset_list tablist;
)

static void build_tablist(char *tabstops)
{
  char *ctx;
  struct offset_list *tablist = &TT.tablist;
  char *s, *ref;
  off_t stop, last_stop;

  /* for every tabstop decode and add to list */
  for (stop = last_stop = 0, s = ref = xstrdup(tabstops); ;
       last_stop = stop, s = NULL) {
    char *tabstop = strtok_r(s, " ,", &ctx);

    if (!tabstop) return;

    stop = xstrtoul(tabstop, NULL, 0);
    if (stop <= last_stop) {
      free(ref);
      toys.exithelp = 1;
      error_exit("tablist ascending order");
    }
    tablist->next = xzalloc(sizeof(*tablist));
    tablist->next->off = stop;
    tablist = tablist->next;
  }

  free(ref);
}

static void expand_file(int fd, char *name)
{
  ssize_t rdn;
  char *rdbuf, *wrbuf;
  size_t wrbuflen, rdbuflen;
  ssize_t rdbufi = 0, wrbufi = 0;
  ssize_t wrlinei;
  int hastablist = !!TT.tablist.next->next;
  struct offset_list *tablist = TT.tablist.next;
  ssize_t stop = tablist->off;

  wrbuflen = rdbuflen = ARRAY_LEN(toybuf)/2;
  rdbuf = toybuf;
  wrbuf = toybuf + rdbuflen;
  do {
    rdn = readall(fd, rdbuf, rdbuflen);
    if (rdn < 0) perror_exit("%s", name);
    for (rdbufi=0, wrbufi=0; rdbufi<rdn; rdbufi++) {
      if (wrbufi == wrbuflen) { /* flush expand buffer when full */
        writeall(STDOUT_FILENO, wrbuf, wrbuflen);
        wrbufi = 0;
      }
      if (rdbuf[rdbufi] == '\t') { /* expand tab */
        size_t count;
        size_t tabsize;

        /* search next tab stop */
        while(tablist && (stop <= wrlinei)) {
          stop = hastablist ? tablist->off : stop + tablist->off;
          tablist = hastablist ? tablist->next : tablist;
        }
        tabsize = ((stop - wrlinei < 2)) ? 1 : stop - wrlinei;
        while (tabsize) { /* long expand */
          count = min(tabsize, wrbuflen - wrbufi);
          memset(wrbuf + wrbufi, ' ', count);
          tabsize -= count;
          if (tabsize) { /* flush expand buffer when full */
            writeall(STDOUT_FILENO, wrbuf, wrbuflen);
            wrbufi = 0;
          } else wrbufi += count;
        }
        wrlinei += count;
      } else { /* copy input to output */
        wrbuf[wrbufi++] = rdbuf[rdbufi];
        wrlinei += 1;
        /* flush expand buffer and reset tablist at newline */
        if (rdbuf[rdbufi] == '\n') {
          writeall(STDOUT_FILENO, wrbuf, wrbufi);
          tablist = TT.tablist.next;
          stop = tablist->off;
          wrbufi = wrlinei = 0;
        }
      }
    }
  } while (rdn == rdbuflen);
  /* flush last expand buffer */
  writeall(STDOUT_FILENO, wrbuf, wrbufi);
}

void expand_main(void)
{
  build_tablist((toys.optflags & FLAG_t) ? TT.t_flags : "8");
  /* expand every file */
  loopfiles(toys.optargs, expand_file);
  /* free tablist */
  llist_traverse(TT.tablist.next, free);
}
