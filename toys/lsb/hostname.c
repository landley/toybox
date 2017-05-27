/* hostname.c - Get/Set the hostname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/hostname.html

USE_HOSTNAME(NEWTOY(hostname, "bF:", TOYFLAG_BIN))

config HOSTNAME
  bool "hostname"
  default y
  help
    usage: hostname [-b] [-F FILENAME] [newname]

    Get/Set the current hostname

    -b	Set hostname to 'localhost' if otherwise unset
    -F	Set hostname to contents of FILENAME
*/

#define FOR_hostname
#include "toys.h"

GLOBALS(
  char *fname;
)

void hostname_main(void)
{
  char *hostname = *toys.optargs;

  if (TT.fname && (hostname = xreadfile(TT.fname, 0, 0))) {
    if (!*chomp(hostname)) {
      if (CFG_TOYBOX_FREE) free(hostname);
      if (!(toys.optflags&FLAG_b)) error_exit("empty '%s'", TT.fname);
      hostname = 0;
    }
  }

  if (!hostname && (toys.optflags&FLAG_b))
    if (gethostname(toybuf, sizeof(toybuf)-1) || !*toybuf)
      hostname = "localhost";

  if (hostname) {
    if (sethostname(hostname, strlen(hostname)))
      perror_exit("set '%s'", hostname);
  } else {
    if (gethostname(toybuf, sizeof(toybuf)-1)) perror_exit("gethostname");
    xputs(toybuf);
  }
}
