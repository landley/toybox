/* uname.c - return system name
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/uname.html

USE_UNAME(NEWTOY(uname, "aomvrns", TOYFLAG_BIN))
USE_ARCH(NEWTOY(arch, 0, TOYFLAG_USR|TOYFLAG_BIN))
USE_LINUX32(NEWTOY(linux32, 0, TOYFLAG_USR|TOYFLAG_BIN))

config ARCH 
  bool "arch"
  default y
  help
    usage: arch

    Print machine (hardware) name, same as uname -m.

config LINUX32
  bool "linux32"
  default y
  help
    usage: linux32 [COMMAND...]

    Tell uname -m to line to autoconf (to build 32 bit binaries on 64 bit kernel).

config UNAME
  bool "uname"
  default y
  help
    usage: uname [-asnrvm]

    Print system information.

    -s	System name
    -n	Network (domain) name
    -r	Kernel Release number
    -v	Kernel Version 
    -m	Machine (hardware) name
    -o	Userspace type
    -a	All of the above (in order)
*/

#define FOR_uname
#define FORCE_FLAGS
#include "toys.h"

void uname_main(void)
{
  int i, needspace = 0;
  char *c;

  uname((void *)toybuf);
  if (!toys.optflags) toys.optflags = FLAG_s;
  for (i=0; i<6; i++) if (toys.optflags & ((1<<i)|FLAG_a)) {
    if (i==5) c = " Toybox"+!needspace;
    else {
      c = toybuf+sizeof(((struct utsname *)0)->sysname)*i;
      if (needspace++) *(--c)=' '; // Can't decrement first entry
    }
    xputsn(c);
  }
  xputc('\n');
}

void arch_main(void)
{
  toys.optflags = FLAG_m;
  uname_main();
}

void linux32_main(void)
{
  personality(PER_LINUX32);
  xexec(toys.optc ? toys.optargs : (char *[]){"/bin/sh", 0});
}
