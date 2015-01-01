// Function to display help text

#include "toys.h"

#if !CFG_TOYBOX_HELP
void show_help(void) {;}
#else
#include "generated/help.h"

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name,opt,flags) help_##name "\0"
#define OLDTOY(name,oldname,flags) "\xff" #oldname "\0"
static char *help_data =
#include "generated/newtoys.h"
;

void show_help(void)
{
  int i = toys.which-toy_list;
  char *s;

  for (;;) {
    s = help_data;
    while (i--) s += strlen(s) + 1;
    // If it's an alias, restart search for real name
    if (*s != 255) break;
    if (!CFG_TOYBOX) {
      s = xmprintf("See %s --help\n", ++s);

      break;
    }
    i = toy_find(++s)-toy_list;
  }

  fprintf(toys.exithelp ? stderr : stdout, "%s", s);
}
#endif
