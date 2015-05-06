/* cat.c - copy inputs to stdout.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cat.html
 *
 * And "Cat -v considered harmful" at
 *   http://cm.bell-labs.com/cm/cs/doc/84/kp.ps.gz

USE_CAT(NEWTOY(cat, "u"USE_CAT_V("vte"), TOYFLAG_BIN))
USE_CATV(NEWTOY(catv, USE_CATV("vte"), TOYFLAG_USR|TOYFLAG_BIN))

config CAT
  bool "cat"
  default y
  help
    usage: cat [-u] [file...]

    Copy (concatenate) files to stdout.  If no files listed, copy from stdin.
    Filename "-" is a synonym for stdin.

    -u	Copy one byte at a time (slow).

config CAT_V
  bool "cat -etv"
  default n
  depends on CAT
  help
    usage: cat [-evt]

    -e	Mark each newline with $
    -t	Show tabs as ^I
    -v	Display nonprinting characters as escape sequences. Use M-x for
    	high ascii characters (>127), and ^x for other nonprinting chars.

config CATV
  bool "catv"
  default y
  help
    usage: catv [-evt] [filename...]

    Display nonprinting characters as escape sequences. Use M-x for
    high ascii characters (>127), and ^x for other nonprinting chars.

    -e  Mark each newline with $
    -t  Show tabs as ^I
    -v  Don't use ^x or M-x escapes.
*/

#define FOR_cat
#define FORCE_FLAGS
#include "toys.h"

static void do_cat(int fd, char *name)
{
  int i, len, size=(toys.optflags & FLAG_u) ? 1 : sizeof(toybuf);

  for(;;) {
    len = read(fd, toybuf, size);
    if (len < 0) toys.exitval = EXIT_FAILURE;
    if (len < 1) break;
    if ((CFG_CAT_V || CFG_CATV) && (toys.optflags&~FLAG_u)) {
      for (i=0; i<len; i++) {
        char c=toybuf[i];

        if (c > 126 && (toys.optflags & FLAG_v)) {
          if (c > 127) {
            printf("M-");
            c -= 128;
          }
          if (c == 127) {
            printf("^?");
            continue;
          }
        }
        if (c < 32) {
          if (c == 10) {
            if (toys.optflags & FLAG_e) xputc('$');
          } else if (toys.optflags & (c==9 ? FLAG_t : FLAG_v)) {
            printf("^%c", c+'@');
            continue;
          }
        }
        xputc(c);
      }
    } else xwrite(1, toybuf, len);
  }
}

void cat_main(void)
{
  loopfiles(toys.optargs, do_cat);
}

void catv_main(void)
{
  toys.optflags ^= FLAG_v;
  loopfiles(toys.optargs, do_cat);
}
