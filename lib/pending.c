/* pending.c - reusable stuff awaiting review
 *
 * new lib entries for stuff in toys/pending
 */

#include "toys.h"

// Execute a callback for each PID that matches a process name from a list.
void for_each_pid_with_name_in(char **names, int (*callback)(pid_t pid, char *name))
{
  DIR *dp;
  struct dirent *entry;
  char cmd[sizeof(toybuf)], path[64];
  char **curname;

  if (!(dp = opendir("/proc"))) perror_exit("opendir");

  while ((entry = readdir(dp))) {
    int fd, n;

    if (!isdigit(*entry->d_name)) continue;

    if (sizeof(path) <= snprintf(path, sizeof(path), "/proc/%s/cmdline",
      entry->d_name)) continue;

    if (-1 == (fd=open(path, O_RDONLY))) continue;
    n = read(fd, cmd, sizeof(cmd));
    close(fd);
    if (n<1) continue;

    for (curname = names; *curname; curname++)
      if (!strcmp(basename(cmd), *curname)) 
          if (!callback(atol(entry->d_name), *curname)) goto done;
  }
done:
  closedir(dp);
}

char* make_human_readable(unsigned long long size, unsigned long unit)
{
  unsigned int frac = 0;
  if(unit) {
    size = (size/(unit)) + (size%(unit)?1:0);
    return xmsprintf("%llu", size);
  }
  else {
    static char units[] = {'\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
    int index = 0;
    while(size >= 1024) {
      frac = size%1024;
      size /= 1024;
      index++;
    }
    frac = (frac/102) + ((frac%102)?1:0);
    if(frac >= 10) {
      size += 1;
      frac = 0;
    }
    if(frac) return xmsprintf("%llu.%u%c", size, frac, units[index]);
    else return xmsprintf("%llu%c", size, units[index]);
  }
  return NULL; //not reached
}

/*
 * used to get the interger value.
 */
unsigned long get_int_value(const char *numstr, unsigned lowrange, unsigned highrange)
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
