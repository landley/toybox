/* xxd.c - hexdump.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * TODO: support for reversing a hexdump back into the original data.
 * TODO: support > 4GiB files?
 * TODO: -s seek

USE_XXD(NEWTOY(xxd, ">1c#<0=16l#g#<0=2", TOYFLAG_USR|TOYFLAG_BIN))

config XXD
  bool "xxd"
  default n
  help
    usage: xxd [-c n] [-g n] [-l n] [file]

    Hexdump a file to stdout.  If no file is listed, copy from stdin.
    Filename "-" is a synonym for stdin.

    -c n	Show n bytes per line (default 16).
    -g n	Group bytes by adding a ' ' every n bytes (default 2).
    -l n	Limit of n bytes before stopping (default is no limit).
*/

#define FOR_xxd
#include "toys.h"

static const char* hex_digits = "0123456789abcdef";

GLOBALS(
  long bytes_per_group; // -g
  long limit;           // -l
  long bytes_per_line;  // -c
)

static void xxd_file(FILE *fp)
{
  // "0000000: 4c69 6e75 7820 7665 7273 696f 6e20 332e  Linux version 3.".
  size_t hex_size = 8 + 2 +
      2*TT.bytes_per_line + TT.bytes_per_line/TT.bytes_per_group + 1;
  size_t line_size = hex_size + TT.bytes_per_line + 1;
  char *line = xmalloc(line_size);
  int offset = 0, bytes_this_line = 0, index = 0, ch;

  memset(line, ' ', line_size);
  line[line_size - 1] = 0;

  while ((ch = getc(fp)) != EOF) {
    if (!bytes_this_line) index = sprintf(line, "%08x: ", offset);
    ++offset;

    line[index++] = hex_digits[(ch >> 4) & 0xf];
    line[index++] = hex_digits[ch & 0xf];
    line[hex_size + bytes_this_line] = (ch >= ' ' && ch <= '~') ? ch : '.';

    ++bytes_this_line;
    if (bytes_this_line == TT.bytes_per_line) {
      puts(line);
      memset(line, ' ', line_size - 1);
      bytes_this_line = 0;
    } else if (!(bytes_this_line % TT.bytes_per_group)) line[index++] = ' ';
    if ((toys.optflags & FLAG_l) && offset == TT.limit) break;
  }
  if (bytes_this_line) {
    line[hex_size + bytes_this_line] = 0;
    puts(line);
  }

  if (CFG_FREE) free(line);
}

void xxd_main(void)
{
  FILE *fp;

  if (!*toys.optargs || !strcmp(*toys.optargs, "-")) fp = stdin;
  else fp = xfopen(*toys.optargs, "r");

  xxd_file(fp);

  fclose(fp);
}
