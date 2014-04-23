/* watch.c - Execute a program periodically
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
USE_WATCH(NEWTOY(watch, "^<1n#<0=2te", TOYFLAG_USR|TOYFLAG_BIN))

config WATCH
  bool "watch"
  default n
  help
    usage: watch [-n SEC] [-t] PROG ARGS

    Run PROG periodically

    -n  Loop period in seconds (default 2)
    -t  Don't print header
    -e  Freeze updates on command error, and exit after enter.
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
  unsigned width = 80, len = sizeof("Www Mmm dd hh:mm:ss yyyy") - 1 ;
  char *header, *cmd = *toys.optargs;
  int retval;

  while(toys.optargs[++i])
  {
    char * oldcmd = cmd;
    cmd = xmprintf("%s %s", oldcmd, toys.optargs[i]);
    if (CFG_TOYBOX_FREE) free(oldcmd);
  }
  header = xmprintf("Every %us: %s", TT.interval, cmd);
  hlen = strlen(header);

  while(1) {
    xprintf("\033[H\033[J");
    if(!(toys.optflags & FLAG_t)) {
      terminal_size(&width, NULL);
      if (!width) width = 80; //on serial it may return 0.
      time(&t);
      if (width > (hlen + len)) xprintf("%s", header);
      if(width >= len)
        xprintf("%*s\n",width + ((width > (hlen + len))?-hlen:0) + 1, ctime(&t));
      else
        xprintf("\n\n");
    }
    fflush(NULL); //making sure the screen is clear
    retval = system(cmd);
    if ((toys.optflags & FLAG_e) && retval){
      xprintf("command exit with non-zero status, press enter to exit\n");
      getchar();
      break;
    }
    sleep(TT.interval);
  }

  if (CFG_TOYBOX_FREE){
    free(header);
    if (cmd != *toys.optargs) free(cmd);
  }
}
