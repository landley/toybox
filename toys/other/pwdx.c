/* pwdx.c - report current directory of a process. 
 *
 * Copyright 2013 Lukasz Skalski <l.skalski@partner.samsung.com>

USE_PWDX(NEWTOY(pwdx, "<1a", TOYFLAG_USR|TOYFLAG_BIN))

config PWDX
  bool "pwdx"
  default y
  help
    usage: pwdx pids ...
*/

#include "toys.h"

int pid_dir(char *pid)
{
  char *path;
  int num_bytes;

  path = xmsprintf("/proc/%s/cwd",pid);
  num_bytes = readlink(path,toybuf,sizeof(toybuf));
  if(num_bytes==-1){
    xprintf("%s: %s\n",pid,strerror(errno));
    return 1;
  }else{
    toybuf[num_bytes]='\0';
    xprintf("%s: %s\n",pid,toybuf);
    return 0;
  }
}

void pwdx_main(void)
{
  int i;

  for (i=0; toys.optargs[i]; i++)
    toys.exitval |= pid_dir(toys.optargs[i]);
}

