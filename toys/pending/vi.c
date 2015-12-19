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

struct linestack {
  long len, max;
  char *line[];
};

// Insert one stack into another before position in old stack.
// (Does not copy contents of strings, just shuffles index array contents.)
void linestack_addstack(struct linestack **lls, struct linestack *throw,
  long pos)
{
  struct linestack *catch = *lls;

  if (CFG_TOYBOX_DEBUG)
    if (pos > catch->len) error_exit("linestack_addstack past end.");

  // Make a hole, allocating more space if necessary.
  if (catch->len+throw->len >= catch->max) {
    // New size rounded up to next multiple of 64, allocate and copy start.
    catch->max = ((catch->len+throw->len)|63)+1;
    *lls = xmalloc(sizeof(struct linestack)+catch->max*sizeof(char *));
    memcpy(*lls, catch, sizeof(struct linestack)+pos*sizeof(char *));
  }

  // Copy end (into new allocation if necessary)
  if (pos != catch->len)
    memmove((*lls)->line+pos+throw->len, catch->line+pos,
      (catch->len-pos)*sizeof(char *));

  // Cleanup if we had to realloc.
  if (catch != *lls) {
    free(catch);
    catch = *lls;
  }

  memcpy(catch->line+pos, throw->line, throw->len*sizeof(char *));
  catch->len += throw->len;
}

void linestack_insert(struct linestack **lls, long pos, char *line)
{
  // alloca() was in 32V and Turbo C for DOS, but isn't in posix or c99.
  // I'm not thrashing the heap for this, but this should work even if
  // a broken compiler adds gratuitous padding.
  struct {
    struct linestack ls;
    char *line;
  } ls;

  ls.ls.len = ls.ls.max = 1;
  *ls.ls.line = line;
  linestack_addstack(lls, &ls.ls, pos);
}

void vi_main(void)
{
  int i;

  TT.ls = xzalloc(sizeof(struct linestack));

  linestack_insert(&TT.ls, 0, "one");
  linestack_insert(&TT.ls, 1, "two");
  linestack_insert(&TT.ls, 2, "three");

  for (i=0; i<TT.ls->len; i++) printf("%s\n", TT.ls->line[i]);  
}
