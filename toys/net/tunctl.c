/* tunctl.c - Control tap/tun network devices.
 *
 * Copyright 2016 Rob Landley <rob@landley.net>
 *
 * See http://kernel.org/doc/Documentation/networking/tuntap.txt

USE_TUNCTL(NEWTOY(tunctl, "<1>1t|d|u:[!td]", TOYFLAG_USR|TOYFLAG_BIN))

config TUNCTL
  bool "tunctl"
  default y
  help
    usage: tunctl [-dt] [-u USER] NAME

    Create and delete tun/tap virtual ethernet devices.
    A tap device Template for new commands. You don't need this.

    -d	Delete tun device
    -t	Create tun device
    -u	Owner of new device
*/

#define FOR_tunctl
#include "toys.h"
#include <linux/if_tun.h>

GLOBALS(
  char *user;
)

void tunctl_main(void)
{
  struct ifreq *ifr = (void *)toybuf;
  uid_t u = TT.user ?  xgetuid(TT.user) : 0;
  int fd = xopen("/dev/net/tun", O_RDWR);

  ifr->ifr_flags = IFF_TAP|IFF_NO_PI;
  strncpy(ifr->ifr_name, *toys.optargs, sizeof(ifr->ifr_name));
  xioctl(fd, TUNSETIFF, toybuf);
  if (toys.optflags&FLAG_t) {
    xioctl(fd, TUNSETPERSIST, (void *)1);
    xioctl(fd, TUNSETOWNER, (void *)(long)u);
  } else xioctl(fd, TUNSETPERSIST, (void *)0);
}
