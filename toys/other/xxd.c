/* xxd.c - hexdump.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * No obvious standard, output looks like:
 * 0000000: 4c69 6e75 7820 7665 7273 696f 6e20 332e  Linux version 3.

USE_XXD(NEWTOY(xxd, ">1c#<1>4096=16l#g#<1=2prs#[!rs]", TOYFLAG_USR|TOYFLAG_BIN))

config XXD
  bool "xxd"
  default y
  help
    usage: xxd [-c n] [-g n] [-l n] [-p] [-r] [-s n] [file]

    Hexdump a file to stdout.  If no file is listed, copy from stdin.
    Filename "-" is a synonym for stdin.

    -c n	Show n bytes per line (default 16)
    -g n	Group bytes by adding a ' ' every n bytes (default 2)
    -l n	Limit of n bytes before stopping (default is no limit)
    -p	Plain hexdump (30 bytes/line, no grouping)
    -r	Reverse operation: turn a hexdump into a binary file
    -s n	Skip to offset n
*/

#define FOR_xxd
#include "toys.h"

GLOBALS(
  long s;
  long g;
  long l;
  long c;
)

static void do_xxd(int fd, char *name)
{
  long long pos = 0;
  long long limit = TT.l;
  int i, len, space;

  if (toys.optflags&FLAG_s) {
    xlseek(fd, TT.s, SEEK_SET);
    pos = TT.s;
    if (limit) limit += TT.s;
  }

  while (0<(len = readall(fd, toybuf,
                          (limit && limit-pos<TT.c)?limit-pos:TT.c))) {
    if (!(toys.optflags&FLAG_p)) printf("%08llx: ", pos);
    pos += len;
    space = 2*TT.c+TT.c/TT.g+1;

    for (i=0; i<len;) {
      space -= printf("%02x", toybuf[i]);
      if (!(++i%TT.g)) {
        putchar(' ');
        space--;
      }
    }

    if (!(toys.optflags&FLAG_p)) {
      printf("%*s", space, "");
      for (i=0; i<len; i++)
        putchar((toybuf[i]>=' ' && toybuf[i]<='~') ? toybuf[i] : '.');
    }
    putchar('\n');
  }
  if (len<0) perror_exit("read");
}

static int dehex(char ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'a' + 10;
  return (ch == '\n') ? -2 : -1;
}

static void do_xxd_reverse(int fd, char *name)
{
  FILE *fp = xfdopen(fd, "r");

  while (!feof(fp)) {
    int col = 0;
    int tmp;

    // Each line of a non-plain hexdump starts with an offset/address.
    if (!(toys.optflags&FLAG_p)) {
      long long pos;

      if (fscanf(fp, "%llx: ", &pos) == 1) {
        if (fseek(stdout, pos, SEEK_SET) != 0) {
          // TODO: just write out zeros if non-seekable?
          perror_exit("%s: seek failed", name);
        }
      }
    }

    // A plain hexdump can have as many bytes per line as you like,
    // but a non-plain hexdump assumes garbage after it's seen the
    // specified number of bytes.
    while (toys.optflags&FLAG_p || col < TT.c) {
      int n1, n2;

      // If we're at EOF or EOL or we read some non-hex...
      if ((n1 = n2 = dehex(fgetc(fp))) < 0 || (n2 = dehex(fgetc(fp))) < 0) {
        // If we're at EOL, start on that line.
        if (n1 == -2 || n2 == -2) continue;
        // Otherwise, skip to the next line.
        break;
      }

      fputc((n1 << 4) | (n2 & 0xf), stdout);
      col++;

      // Is there any grouping going on? Ignore a single space.
      tmp = fgetc(fp);
      if (tmp != ' ') ungetc(tmp, fp);
    }

    // Skip anything else on this line (such as the ASCII dump).
    while ((tmp = fgetc(fp)) != EOF && tmp != '\n')
      ;
  }
  if (ferror(fp)) perror_msg_raw(name);

  fclose(fp);
}

void xxd_main(void)
{
  // Plain style is 30 bytes/line, no grouping.
  if (toys.optflags&FLAG_p) TT.c = TT.g = 30;

  loopfiles(toys.optargs, toys.optflags&FLAG_r ? do_xxd_reverse : do_xxd);
}
