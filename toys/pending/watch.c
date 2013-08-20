/* watch.c - Execute a program periodically
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
USE_WATCH(NEWTOY(watch, "^<1n#<0=2t", TOYFLAG_USR|TOYFLAG_BIN))

config WATCH
  bool "watch"
  default n
  help
    Usage: watch [-n SEC] [-t] PROG ARGS

    Run PROG periodically

    -n  Loop period in seconds (default 2)
    -t  Don't print header
*/
#define FOR_watch
#include "toys.h"

GLOBALS(
  int interval; 
)

void watch_main(void)
{
  int i = 0, hlen;
  time_t t;
  unsigned width = 80, len = sizeof("1234-67-90 23:56:89");//time format
  char *header, *cmd = *toys.optargs;

  while(toys.optargs[++i]) cmd = xmsprintf("%s %s", cmd, toys.optargs[i]);
  header = xmsprintf("Every %us: %s", TT.interval, cmd);

  while(1) {
    xprintf("\033[H\033[J");
    if(!(toys.optflags & FLAG_t)) {
      xprintf("%s", header);
      hlen = strlen(header);
      terminal_size(&width, NULL);
      if (!width) width = 80; //on serial it may return 0.
      if (width > (hlen + len)) {                         
        time(&t);                                         
        strftime(toybuf, len, "%Y-%m-%d %H:%M:%S", localtime(&t));
        xprintf("%*s", width - hlen, toybuf);             
      }
      xprintf("\n\n"); // 1'\n' for space between header and result
    }
    fflush(NULL); //making sure the screen is clear
    system(cmd);
    sleep(TT.interval);
  }
  if (CFG_TOYBOX_FREE) free(header);
}
