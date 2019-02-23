/* mcookie - generate a 128-bit random number (used for X "magic cookies")
 *
 * Copyright 2019 AD Isaac Dunham <ibid.ag@gmail.com>
 *
 * No standard.
 * util-linux mcookie originally found the md5sum of several files in /proc
 * and reported that; more recent versions use the best random number source
 * and find the md5sum, thus wasting entropy.
 * We just ask the system for 128 bits and print it.
 *
 *
USE_MCOOKIE(NEWTOY(mcookie, "v(verbose)V(version)", TOYFLAG_USR|TOYFLAG_BIN))

config MCOOKIE
  bool "mcookie"
  default n
  help
    usage: mcookie [-v | -V]

    Generate a 128-bit random number from system sources.
    -f and -m are not supported; md5 sums of arbitrary files are not a
        good source of entropy
    -h  show help
    -v  show entropy source (verbose)
    -V  show version
*/

#define FOR_mcookie
#include "toys.h"

void mcookie_main(void)
{
  int i;
  if (toys.optflags & FLAG_V) {
    puts("mcookie from toybox");
    return;
  }
  xgetrandom(toybuf, 16, 0);
  if (toys.optflags & FLAG_v) {
    fputs("Got 16 bytes from xgetrandom()\n", stderr);
  }
  for (i = 0; i < 16; i++) {
    sprintf(toybuf+16+2*i,"%02x", toybuf[i]);
  }
  toybuf[48] = '\0';
  puts(toybuf + 16);
}
