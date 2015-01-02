/* Wrapper to make installation easier with cross-compiling.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) {#name, 0, 0, flags},
#define OLDTOY(name, oldname, flags) {#name, 0, 0, flags},

// Populate toy_list[].

struct toy_list toy_list[] = {
#include "generated/newtoys.h"
};

#define TOY_LIST_LEN (sizeof(toy_list)/sizeof(struct toy_list))

int main(int argc, char *argv[])
{
  static char *toy_paths[]={"usr/","bin/","sbin/",0};
  int i, len = 0;

  // Output list of applets.
  for (i=1; i<TOY_LIST_LEN; i++) {
    int fl = toy_list[i].flags;
    if (fl & TOYMASK_LOCATION) {
      if (argc>1) {
        int j;
        for (j=0; toy_paths[j]; j++)
          if (fl & (1<<j)) len += printf("%s", toy_paths[j]);
      }
      len += printf("%s\n",toy_list[i].name);
    }
  }
  return 0;
}
