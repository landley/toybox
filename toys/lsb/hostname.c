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

    -b  Set hostname to 'localhost' if otherwise unset
    -F  Set hostname to contents of FILENAME
*/

#define FOR_hostname
#include "toys.h"

GLOBALS(
  char *fname;
)

void hostname_main(void)
{
  const char *hostname = toys.optargs[0];

  if (toys.optflags & FLAG_F) {
    char *buf;
    if ((hostname = buf = readfile(TT.fname, 0, 0))) {
      size_t len = strlen(hostname);
      char *end = buf + len - 1;

      /* Trim trailing whitespace. */
      while (len && isspace(*end)) {
        *end-- = '\0';
        len--;
      }
      if (!len) {
        free(buf);
        hostname = NULL;
        if (!(toys.optflags & FLAG_b))
          error_exit("empty file '%s'", TT.fname);
      }
    } else if (!(toys.optflags & FLAG_b))
      error_exit("failed to read '%s'", TT.fname);
  }

  if (!hostname && toys.optflags & FLAG_b) {
    /* Do nothing if hostname already set. */
    if (gethostname(toybuf, sizeof(toybuf))) perror_exit("get failed");
    if (strnlen(toybuf, sizeof(toybuf))) exit(0);

    /* Else set hostname to localhost. */
    hostname = "localhost";
  }

  if (hostname) {
    if (sethostname(hostname, strlen(hostname)))
      perror_exit("set failed '%s'", hostname);
  } else {
    if (gethostname(toybuf, sizeof(toybuf))) perror_exit("get failed");
    xputs(toybuf);
  }
}
