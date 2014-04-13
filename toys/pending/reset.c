/* reset.c - A program to reset the terminal.
 *
 * Copyright 2014 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_RESET(NEWTOY(reset, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config RESET
  bool "reset"
  default n
  help
    usage: reset

    A program to reset the terminal.
*/
#define FOR_reset
#include "toys.h"

void reset_main(void)
{
  char *args[] = {"stty", "sane", NULL};

  /* \033c - reset the terminal with default setting
   * \033(B - set the G0 character set (B=US)
   * \033[2J - clear the whole screen
   * \033[0m - Reset all attributes
   */
  if (isatty(1)) xprintf("\033c\033(B\033[0m\033[J\033[?25h");
  fflush(stdout);
  // set the terminal to sane settings
  xexec(args);
}
