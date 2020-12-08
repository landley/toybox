/* pwgen.c - A password generator.
 *
 * Copyright 2020 Moritz Röhrich <moritz@ildefons.de>

USE_PWGEN(NEWTOY(pwgen, ">2r(remove):c(capitalize)n(numerals)y(symbols)s(secure)B(ambiguous)h(help)C1vA(no-capitalize)0(no-numerals)[-cA][-n0][-C1]", TOYFLAG_USR|TOYFLAG_BIN))

config PWGEN
  bool "pwgen"
  default y
  help
    usage: pwgen [-cAn0yrsBhC1v] [LENGTH] [COUNT]

    Generate human-readable random passwords. When output is to tty produces
    a screenfull to defeat shoulder surfing (pick one and clear the screen).

    -c  --capitalize                  Permit capital letters.
    -A  --no-capitalize               Don't include capital letters.
    -n  --numerals                    Permit numbers.
    -0  --no-numerals                 Don't include numbers.
    -y  --symbols                     Permit special characters ($#%...).
    -r <chars>  --remove=<chars>      Don't include the given characters.
    -s  --secure                      Generate more random passwords.
    -B  --ambiguous                   Avoid ambiguous characters (e.g. 0, O).
    -h  --help                        Print this help message.
    -C                                Print the output in columns.
    -1                                Print the output one line each.
    -v                                Don't include vowels.
*/

#define FOR_pwgen
#include "toys.h"

GLOBALS(
  char *r;
)

void pwgen_main(void)
{
  int length = 8, count, ii, jj, c, rand = 0, x = 0;
  unsigned xx = 80, yy = 24;
  char randbuf[16];

  if (isatty(1)) terminal_size(&xx, &yy);
  else toys.optflags |= FLAG_1;

  if (toys.optc && (length = atolx(*toys.optargs))>sizeof(toybuf))
    error_exit("bad length");
  if (toys.optc>1) count = atolx(toys.optargs[1]);
  else count = FLAG(1) ? 1 : (xx/(length+1))*(yy-1);

  for (jj = 0; jj<count; jj++) {
    for (ii = 0; ii<length;) {
      // Don't fetch more random than necessary, give each byte 2 tries to fit
      if (!rand) xgetrandom(randbuf, rand = sizeof(randbuf), 0);
      c = 33+randbuf[--rand]%93; // remainder 69 makes >102 less likely
      if (FLAG(s)) randbuf[rand] = 0;

      if (c>='A' && c<='Z') {
        if (FLAG(A)) continue;
        // take out half the capital letters to be more human readable
        else c |= (0x80&randbuf[rand])>>2;
      }
      if (FLAG(0) && c>='0' && c<='9') continue;
      if (FLAG(B) && strchr("0O1lI'`.,", c)) continue;
      if (FLAG(v) && strchr("aeiou", tolower(c))) continue;
      if (!FLAG(y) || (0x80&randbuf[rand]))
        if (c<'0' || (c>'9' && c<'A') || (c>'Z' && c<'a') || c>'z') continue;
      if (TT.r && strchr(TT.r, c)) continue;

      toybuf[ii++] = c;
    }
    if (FLAG(1) || (x += length+1)+length>=xx) x = 0;
    xprintf("%.*s%c", length, toybuf, x ? ' ' : '\n');
  }
  if (x) xputc('\n');
}
