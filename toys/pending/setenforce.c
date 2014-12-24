/* setenforce.c - Set the current SELinux mode
 *
 * Copyright 2014 The Android Open Source Project

USE_SETENFORCE(NEWTOY(setenforce, "<1", TOYFLAG_USR|TOYFLAG_SBIN))

config SETENFORCE
  bool "setenforce"
  default n
  help
    usage: setenforce [enforcing|permissive|1|0]

    Sets whether SELinux is enforcing (1) or permissive (0).
*/

#define FOR_setenforce
#include "toys.h"
#include <selinux/selinux.h>

void setenforce_main(void)
{
  char *state_str = *toys.optargs;
  int state;
  if (!is_selinux_enabled())
    error_exit("SELinux is disabled");
  else if (!strcmp(state_str, "1") || !strcasecmp(state_str, "enforcing"))
    state = 1;
  else if (!strcmp(state_str, "0") || !strcasecmp(state_str, "permissive"))
    state = 0;
  else
    error_exit("Invalid state: %s", state_str);

  int ret = security_setenforce(state);
  if (ret == -1)
    perror_msg("Couldn't set enforcing status to '%s'", state_str);
}
