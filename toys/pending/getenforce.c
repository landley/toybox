/* getenforce.c - Get the current SELinux mode
 *
 * Copyright 2014 The Android Open Source Project

USE_GETENFORCE(NEWTOY(getenforce, "", TOYFLAG_USR|TOYFLAG_SBIN))

config GETENFORCE
  bool "getenforce"
  default n
  help
    usage: getenforce

    Shows whether SELinux is disabled, enforcing, or permissive.
*/

#define FOR_getenforce
#include "toys.h"
#include <selinux/selinux.h>

void getenforce_main(void)
{
  if (!is_selinux_enabled())
    printf("Disabled\n");
  else {
    int ret = security_getenforce();
    if (ret == -1)
      perror_exit("Couldn't get enforcing status");
    else
      printf(ret ? "Enforcing\n" : "Permissive\n");
  }
}
