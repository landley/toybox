/* comm.c - select or reject lines common to two files
 *
 * Copyright 2012 Ilya Kuzmich <ikv@safe-mail.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/comm.html

// <# and ># take single digit, so 321 define flags
USE_COMM(NEWTOY(comm, "<2>2321", TOYFLAG_USR|TOYFLAG_BIN))

config COMM
  bool "comm"
  default y
  help
    usage: comm [-123] FILE1 FILE2

    Reads FILE1 and FILE2, which should be ordered, and produces three text
    columns as output: lines only in FILE1; lines only in FILE2; and lines
    in both files. Filename "-" is a synonym for stdin.

    -1 suppress the output column of lines unique to FILE1
    -2 suppress the output column of lines unique to FILE2
    -3 suppress the output column of lines duplicated in FILE1 and FILE2
*/

#define FOR_comm
#include "toys.h"

static void writeline(const char *line, int col)
{
  if (col == 0 && toys.optflags & FLAG_1) return;
  else if (col == 1) {
    if (toys.optflags & FLAG_2) return;
    if (!(toys.optflags & FLAG_1)) putchar('\t');
  } else if (col == 2) {
    if (toys.optflags & FLAG_3) return;
    if (!(toys.optflags & FLAG_1)) putchar('\t');
    if (!(toys.optflags & FLAG_2)) putchar('\t');
  }
  puts(line);
}

void comm_main(void)
{
  int file[2];
  char *line[2];
  int i;

  if (toys.optflags == 7) return;

  for (i = 0; i < 2; i++) {
    file[i] = strcmp("-", toys.optargs[i])
      ? xopen(toys.optargs[i], O_RDONLY) : 0;
    line[i] = get_line(file[i]);
  }

  while (line[0] && line[1]) {
    int order = strcmp(line[0], line[1]);

    if (order == 0) {
      writeline(line[0], 2);
      for (i = 0; i < 2; i++) {
        free(line[i]);
        line[i] = get_line(file[i]);
      }
    } else {
      i = order < 0 ? 0 : 1;
      writeline(line[i], i);
      free(line[i]);
      line[i] = get_line(file[i]);
    }
  }

  /* print rest of the longer file */
  for (i = line[0] ? 0 : 1; line[i];) {
    writeline(line[i], i);
    free(line[i]);
    line[i] = get_line(file[i]);
  }

  if (CFG_TOYBOX_FREE) for (i = 0; i < 2; i++) xclose(file[i]);
}
