/* reset.c - reset the terminal.
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * No standard.

USE_RESET(NEWTOY(reset, 0, TOYFLAG_USR|TOYFLAG_BIN))

config RESET
  bool "reset"
  default y
  help
    usage: reset

    reset the terminal
*/
#include "toys.h"

void reset_main(void)
{
  // man 4 console codes: reset terminal is ESC (no left bracket) c
  xwrite(xgettty(), "\033c", 2);
}
