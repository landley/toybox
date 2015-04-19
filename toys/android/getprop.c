/* getprop.c - Get an Android system property
 *
 * Copyright 2015 The Android Open Source Project

USE_GETPROP(NEWTOY(getprop, ">2", TOYFLAG_USR|TOYFLAG_SBIN))

config GETPROP
  bool "getprop"
  default y
  help
    usage: getprop [NAME [DEFAULT]]

    Gets an Android system property, or lists them all.
*/

#define FOR_getprop
#include "toys.h"

#if defined(__ANDROID__)

#include <cutils/properties.h>

GLOBALS(
  size_t size;
  size_t capacity;
)

struct property_info {
  char *name;
  char *value;
};

static struct property_info **properties;

static void add_property(const char *name, const char *value, void *unused)
{
  struct property_info *new = xmalloc(sizeof(struct property_info));

  if (TT.size >= TT.capacity) {
    TT.capacity += 32;
    properties = xrealloc(properties,
        TT.capacity * sizeof(struct property_info *));
  }

  // TODO: fix xstrdup signature so we can remove these bogus casts.
  new->name = xstrdup((char *) name);
  new->value = xstrdup((char *) value);
  properties[TT.size++] = new;
}

static void free_properties()
{
  size_t i;

  for (i = 0; i < TT.size; ++i) {
    free(properties[i]->name);
    free(properties[i]->value);
    free(properties[i]);
  }
  free(properties);
}

static int property_cmp(const void *a, const void *b)
{
  struct property_info *pa = *((struct property_info **)a);
  struct property_info *pb = *((struct property_info **)b);

  return strcmp(pa->name, pb->name);
}

void getprop_main(void)
{
  if (*toys.optargs) {
    char value[PROPERTY_VALUE_MAX];
    const char *default_value = "";

    if (toys.optargs[1]) default_value = toys.optargs[1];
    property_get(*toys.optargs, value, default_value);
    puts(value);
  } else {
    size_t i;

    if (property_list(add_property, NULL))
      error_exit("property_list failed");
    qsort(properties, TT.size, sizeof(struct property_info *), property_cmp);
    for (i = 0; i < TT.size; ++i)
      printf("[%s]: [%s]\n", properties[i]->name, properties[i]->value);
    if (CFG_TOYBOX_FREE) free_properties();
  }
}

#else

void getprop_main(void)
{
}

#endif
