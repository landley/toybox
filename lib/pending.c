/* pending.c - reusable stuff awaiting review
 *
 * new lib entries for stuff in toys/pending
 */

#include "toys.h"

// Execute a callback for each PID that matches a process name from a list.
void names_to_pid(char **names, int (*callback)(pid_t pid, char *name))
{
  DIR *dp;
  struct dirent *entry;

  if (!(dp = opendir("/proc"))) perror_exit("opendir");

  while ((entry = readdir(dp))) {
    unsigned u;
    char *cmd, **curname;

    if (!(u = atoi(entry->d_name))) continue;
    sprintf(libbuf, "/proc/%u/cmdline", u);
    if (!(cmd = readfile(libbuf, libbuf, sizeof(libbuf)))) continue;

    for (curname = names; *curname; curname++)
      if (**curname == '/' ? !strcmp(cmd, *curname)
          : !strcmp(basename(cmd), basename(*curname)))
        if (callback(u, *curname)) break;
    if (*curname) break;
  }
  closedir(dp);
}

/*
 * used to get the interger value.
 */
unsigned long get_int_value(const char *numstr, unsigned long lowrange, unsigned long highrange)
{
  unsigned long rvalue = 0;
  char *ptr;

  if (!isdigit(*numstr)) perror_exit("bad number '%s'", numstr);
  errno = 0;
  rvalue = strtoul(numstr, &ptr, 10);

  if (errno || numstr == ptr || *ptr || rvalue < lowrange || rvalue > highrange)
    perror_exit("bad number '%s'", numstr);

  return rvalue;
}

void daemonize(void)
{
  int fd = open("/dev/null", O_RDWR);
  if (fd < 0) fd = xcreate("/", O_RDONLY, 0666);

  pid_t pid = fork();
  if (pid < 0) perror_exit("DAEMON: failed to fork");
  if (pid) exit(EXIT_SUCCESS);

  setsid();
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  if (fd > 2) close(fd);
}
