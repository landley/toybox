/* demo_utf8towc() against libc mbrtowc()
 *
 * Copyright 2017 Rob Landley <rob@landley.net>

USE_DEMO_UTF8TOWC(NEWTOY(demo_utf8towc, 0, TOYFLAG_USR|TOYFLAG_BIN))

config DEMO_UTF8TOWC
  bool "demo_utf8towc"
  default n
  help
    usage: demo_utf8towc

    Print differences between toybox's utf8 conversion routines vs libc du jour.
*/

#include "toys.h"

void demo_utf8towc_main(void)
{
  mbstate_t mb;
  int len1, len2;
  unsigned u, h, wc2;
  wchar_t wc1;

  memset(&mb, 0, sizeof(mb));
  for (u = 1; u<=0x10ffff; u++) {
    char *str = (void *)&h;

    wc1 = wc2 = 0;
    len2 = 4;
    h = htonl(u);
    while (!*str) str++, len2--;

    len1 = mbrtowc(&wc1, str, len2, &mb);
    if (len1<0) memset(&mb, 0, sizeof(mb));
    len2 = utf8towc(&wc2, str, len2);
    if (len1 != len2 || wc1 != wc2)
      printf("%x %d %x %d %x\n", u, len1, wc1, len2, wc2);
  }
}
