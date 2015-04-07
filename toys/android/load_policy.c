/* load_policy.c - Load a policy file
 *
 * Copyright 2015 The Android Open Source Project

USE_LOAD_POLICY(NEWTOY(load_policy, "<1>1", TOYFLAG_USR|TOYFLAG_SBIN))

config LOAD_POLICY
  bool "load_policy"
  depends on TOYBOX_SELINUX
  default y
  help
    usage: load_policy FILE

    Load the specified policy file.
*/

#define FOR_load_policy
#include "toys.h"

void load_policy_main(void)
{
  char *path = *toys.optargs;
  int fd = xopen(path, O_RDONLY);
  off_t policy_len = fdlength(fd);
  char *policy_data = mmap(0, policy_len, PROT_READ, MAP_PRIVATE, fd, 0);

  close(fd);
  if (!policy_data || security_load_policy(policy_data, policy_len) < 0)
    perror_exit("Couldn't %s %s", policy_data ? "load" : "read", path);

  munmap(policy_data, policy_len);
}
