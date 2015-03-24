/* runcon.c - Run command in specified security context
 *
 * Copyright 2015 The Android Open Source Project

USE_LOAD_POLICY(NEWTOY(load_policy, "<1>1", TOYFLAG_USR|TOYFLAG_SBIN))

config LOAD_POLICY
  bool "load_policy"
  depends on TOYBOX_SELINUX
  default n
  help
    usage: load_policy FILE

    Load the specified policy file.
*/

#define FOR_load_policy
#include "toys.h"

void load_policy_main(void)
{
  char *path = *toys.optargs;
  char *policy_data = 0;
  off_t policy_len;
  int fd;

  if ((fd = open(path, O_RDONLY)) != -1) {
    policy_len = fdlength(fd);
    policy_data = mmap(0, policy_len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
  }

  if (!policy_data) {
    error_exit("Couldn't read %s: %s", path, strerror(errno));
  }

  if (security_load_policy(policy_data, policy_len) < 0)
    error_exit("Couldn't load %s: %s", path, strerror(errno));

  munmap(policy_data, policy_len);
}
