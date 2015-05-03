/* getprop.c - Get an Android system property
 *
 * Copyright 2015 The Android Open Source Project

USE_GETPROP(NEWTOY(getprop, ">2", TOYFLAG_USR|TOYFLAG_SBIN))

config GETPROP
  bool "getprop"
  default y
  depends on TOYBOX_ON_ANDROID
  help
    usage: getprop [NAME [DEFAULT]]

    Gets an Android system property, or lists them all.
*/

#define FOR_getprop
#include "toys.h"

#include <cutils/properties.h>

GLOBALS(
  size_t size;
  char **nv; // name/value pairs: even=name, odd=value
)

static void add_property(char *name, char *value, void *unused)
{
  if (!(TT.size&31)) TT.nv = xrealloc(TT.nv, (TT.size+32)*2*sizeof(char *));

  TT.nv[2*TT.size] = xstrdup(name);
  TT.nv[1+2*TT.size++] = xstrdup(value);
}

void getprop_main(void)
{
  if (*toys.optargs) {
    property_get(*toys.optargs, toybuf, toys.optargs[1] ? toys.optargs[1] : "");
    puts(toybuf);
  } else {
    size_t i;

    if (property_list((void *)add_property, 0)) error_exit("property_list");
    qsort(TT.nv, TT.size, 2*sizeof(char *), qstrcmp);
    for (i = 0; i<TT.size; i++) printf("[%s]: [%s]\n", TT.nv[i*2],TT.nv[1+i*2]);
    if (CFG_TOYBOX_FREE) free(TT.nv);
  }
}
