/* pending.c - reusable stuff awaiting review
 *
 * new lib entries for stuff in toys/pending
 */

#include "toys.h"

// Execute a callback for each PID that matches a process name from a list.
void name_to_pid(char **names, int (*callback)(pid_t pid, char *name))
{
  DIR *dp;
  struct dirent *entry;

  if (!(dp = opendir("/proc"))) perror_exit("opendir");

  while ((entry = readdir(dp))) {
    int fd, n;
    unsigned u;
    char *cmd, **curname;

    if (!(u = atoi(entry->d_name))) continue;
    sprintf(libbuf, "/proc/%u/cmdline", u);
    if (!(cmd = readfile(libbuf, libbuf, sizeof(libbuf)))) continue;

    for (curname = names; *curname; curname++)
      if (*curname == '/' ? !strcmp(cmd, *curname)
          : !strcmp(basename(cmd), basename(*curname))
        if (!callback(u, *curname)) break;
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
  if(*numstr == '-' || *numstr == '+' || isspace(*numstr)) perror_exit("invalid number '%s'", numstr);
  errno = 0;
  rvalue = strtoul(numstr, &ptr, 10);
  if(errno || numstr == ptr) perror_exit("invalid number '%s'", numstr);
   if(*ptr) perror_exit("invalid number '%s'", numstr);
   if(rvalue >= lowrange && rvalue <= highrange) return rvalue;
   else {
         perror_exit("invalid number '%s'", numstr);
         return rvalue; //Not reachable; to avoid waring message.
   }
}

/*
 * strcat to mallocated buffer
 * reallocate if need be
 */
char *astrcat (char *x, char *y) {
  char *z;
  z = x;
  x = realloc (x, (x ? strlen (x) : 0) + strlen (y) + 1);
  if (!x) return 0;
  (z ? strcat : strcpy) (x, y);
  return x;
}

/*
 * astrcat, but die on failure
 */
char *xastrcat (char *x, char *y) {
  x = astrcat (x, y);
  if (!x) error_exit ("xastrcat");
  return x;
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
