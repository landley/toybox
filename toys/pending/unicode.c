/* unicode.c - convert between Unicode and UTF-8
 *
 * Copyright 2020 The Android Open Source Project.
 *
 * Loosely based on the Plan9/Inferno unicode(1).

USE_UNICODE(NEWTOY(unicode, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config UNICODE
  bool "unicode"
  default n
  help
    usage: unicode [[min]-max]

    Convert between Unicode code points and UTF-8, in both directions.
*/

#define FOR_unicode
#include "toys.h"

static void codepoint(unsigned wc) {
  char *low="NULSOHSTXETXEOTENQACKBELBS HT LF VT FF CR SO SI DLEDC1DC2DC3DC4"
            "NAKSYNETBCANEM SUBESCFS GS RS US ";
  unsigned n, i;

  printf("U+%04X : ", wc);
  if (wc < ' ') printf("%.3s", low+(wc*3));
  else if (wc == 0x7f) printf("DEL");
  else {
    toybuf[n = wctoutf8(toybuf, wc)] = 0;
    printf("%s%s", toybuf, n>1 ? " :":"");
    if (n>1) for (i = 0; i < n; i++) printf(" %#02x", toybuf[i]);
  }
  xputc('\n');
}

void unicode_main(void)
{
  unsigned from, to;
  char next, **args;

  for (args = toys.optargs; *args; args++) {
    // unicode 660-666 => table of `U+0600 : ٠ : 0xd9 0xa0` etc.
    if (sscanf(*args, "%x-%x%c", &from, &to, &next) == 2) {
      while (from <= to) codepoint(from++);

    // unicode 666 => just `U+0666 : ٦ : 0xd9 0xa6`.
    } else if (sscanf(*args, "%x%c", &from, &next) == 1) {
      codepoint(from);

    // unicode hello => table showing every character in the string.
    } else {
      char *s = *args;
      size_t l = strlen(s);
      wchar_t wc;
      int n;

      while ((n = utf8towc(&wc, s, l)) > 0) {
        codepoint(wc);
        s += n;
        l -= n;
      }
    }
  }
}
