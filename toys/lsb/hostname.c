/* hostname.c - Get/Set the hostname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/hostname.html

USE_HOSTNAME(NEWTOY(hostname, ">1bdsfF:[!bdsf]", TOYFLAG_BIN))

config HOSTNAME
  bool "hostname"
  default y
  help
    usage: hostname [-bsf] [-F FILENAME] [newname]

    Get/set the current hostname.

    -b	Set hostname to 'localhost' if otherwise unset
    -d	Show DNS domain name (no host)
    -f	Show fully-qualified name (host+domain, FQDN)
    -F	Set hostname to contents of FILENAME
    -s	Show short host name (no domain)
*/

#define FOR_hostname
#include "toys.h"

GLOBALS(
  char *F;
)

void hostname_main(void)
{
  char *hostname = *toys.optargs, *dot;
  struct hostent *h;

  if (TT.F && (hostname = xreadfile(TT.F, 0, 0))) {
    if (!*chomp(hostname)) {
      if (CFG_TOYBOX_FREE) free(hostname);
      if (!(toys.optflags&FLAG_b)) error_exit("empty '%s'", TT.F);
      hostname = 0;
    }
  }

  // Implement -b.
  if (!hostname && (toys.optflags&FLAG_b))
    if (gethostname(toybuf, sizeof(toybuf)-1) || !*toybuf)
      hostname = "localhost";

  // Setting?
  if (hostname) {
    if (sethostname(hostname, strlen(hostname)))
      perror_exit("set '%s'", hostname);
    return;
  }

  // Get the hostname.
  if (gethostname(toybuf, sizeof(toybuf)-1)) perror_exit("gethostname");
  if (!(h = gethostbyname(toybuf))) perror_exit("gethostbyname");
  snprintf(toybuf, sizeof(toybuf), "%s", h->h_name);
  dot = strchr(toybuf, '.');
  if (toys.optflags&FLAG_s) *dot = '\0';
  xputs(toys.optflags&FLAG_d ? dot+1 : toybuf);
}
