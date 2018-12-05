/* wc.c - Word count
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/wc.html

USE_WC(NEWTOY(wc, "mcwl", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config WC
  bool "wc"
  default y
  help
    usage: wc -lwcm [FILE...]

    Count lines, words, and characters in input.

    -l	Show lines
    -w	Show words
    -c	Show bytes
    -m	Show characters

    By default outputs lines, words, bytes, and filename for each
    argument (or from stdin if none). Displays only either bytes
    or characters.
*/

#define FOR_wc
#include "toys.h"

GLOBALS(
  unsigned long totals[4];
)

static void show_lengths(unsigned long *lengths, char *name)
{
  int i, space = 0, first = 1;

  // POSIX says there should never be leading spaces, but accepts that
  // traditional implementations use 7 spaces, unless only one file (or
  // just stdin) is being counted, when there should be no leading spaces,
  // *except* for the case where we're going to output multiple numbers.
  // And, yes, folks have test scripts that rely on all this nonsense :-(
  // Note: sufficiently modern versions of coreutils wc will use the smallest
  // column width necessary to have all columns be equal width rather than 0.
  if (!(!toys.optc && !(toys.optflags & (toys.optflags-1))) && toys.optc!=1)
    space = 7;

  for (i = 0; i<4; i++) {
    if (toys.optflags&(1<<i)) {
      printf(" %*ld"+first, space, lengths[i]);
      first = 0;
    }
    TT.totals[i] += lengths[i];
  }
  if (*toys.optargs) printf(" %s", name);
  xputc('\n');
}

static void do_wc(int fd, char *name)
{
  int len = 0, clen = 1, space = 0;
  unsigned long word = 0, lengths[] = {0,0,0,0};

  // Speed up common case: wc -c normalfile is file length.
  if (toys.optflags == FLAG_c) {
    struct stat st;

    // On Linux, files in /proc often report their size as 0.
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size) {
      lengths[2] = st.st_size;
      goto show;
    }
  }

  for (;;) {
    int pos, done = 0, len2 = read(fd, toybuf+len, sizeof(toybuf)-len);

    if (len2<0) perror_msg_raw(name);
    else len += len2;
    if (len2<1) done++;

    for (pos = 0; pos<len; pos++) {
      if (toybuf[pos]=='\n') lengths[0]++;
      lengths[2]++;
      if (FLAG(m)) {
        // If we've consumed next wide char
        if (--clen<1) {
          wchar_t wchar;

          // next wide size, don't count invalid, fetch more data if necessary
          clen = utf8towc(&wchar, toybuf+pos, len-pos);
          if (clen == -1) continue;
          if (clen == -2 && !done) break;

          lengths[3]++;
          space = iswspace(wchar);
        }
      } else space = isspace(toybuf[pos]);

      if (space) word=0;
      else {
        if (!word) lengths[1]++;
        word=1;
      }
    }
    if (done) break;
    if (pos != len) memmove(toybuf, toybuf+pos, len-pos);
    len -= pos;
  }

show:
  show_lengths(lengths, name);
}

void wc_main(void)
{
  if (!toys.optflags) toys.optflags = FLAG_l|FLAG_w|FLAG_c;
  loopfiles(toys.optargs, do_wc);
  if (toys.optc>1) show_lengths(TT.totals, "total");
}
