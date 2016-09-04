// Function to display help text

#include "toys.h"

#include "generated/help.h"

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name,opt,flags) HELP_##name "\0"
#if CFG_TOYBOX
#define OLDTOY(name,oldname,flags) "\xff" #oldname "\0"
#else
#define OLDTOY(name, oldname, flags) HELP_##oldname "\0"
#endif
static char *help_data =
#include "generated/newtoys.h"
;

void show_help(FILE *out)
{
  int i = toys.which-toy_list;
  char *s;

  if (CFG_TOYBOX_HELP) {
    for (;;) {
      s = help_data;
      while (i--) s += strlen(s) + 1;
      // If it's an alias, restart search for real name
      if (*s != 255) break;
      i = toy_find(++s)-toy_list;
    }

    fprintf(out, "%s", s);
  }
}
