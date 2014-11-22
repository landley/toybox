/* acpi.c - show power state
 *
 * Written by Isaac Dunham, 2013
 *
 * No standard.

USE_ACPI(NEWTOY(acpi, "abctV", TOYFLAG_USR|TOYFLAG_BIN))

config ACPI
  bool "acpi"
  default y
  help
    usage: acpi [-abctV]
    
    Show status of power sources and thermal devices.

    -a	show power adapters
    -b	show batteries
    -c	show cooling device state
    -t	show temperatures
    -V	show everything
*/

#define FOR_acpi
#include "toys.h"

GLOBALS(
  int ac;
  int bat;
  int therm;
  int cool;
  char *cpath;
)

int read_int_at(int dirfd, char *name)
{
  int fd, ret=0;
  FILE *fil;

  if ((fd = openat(dirfd, name, O_RDONLY)) < 0) return -1;
  if (!fscanf(fil = xfdopen(fd, "r"), "%d", &ret)) perror_exit("%s", name);
  fclose(fil);

  return ret;
}

int acpi_callback(struct dirtree *tree)
{
  int dfd, fd, len, on;

  errno = 0;

  if (tree->name[0]=='.') return 0;

  if (!tree->parent)
    return DIRTREE_RECURSE|DIRTREE_SYMFOLLOW;

  if (0 <= (dfd = open((TT.cpath=dirtree_path(tree, NULL)), O_RDONLY))) {
    if ((fd = openat(dfd, "type", O_RDONLY)) < 0) goto done;
    len = readall(fd, toybuf, sizeof(toybuf));
    close(fd);
    if (len < 1) goto done;

    if (!strncmp(toybuf, "Battery", 7)) {
      if ((toys.optflags & FLAG_b) || (!toys.optflags)) {
        int cap = 0, curr = 0, max = 0;

        if ((cap = read_int_at(dfd, "capacity")) < 0) {
          if ((max = read_int_at(dfd, "charge_full")) > 0)
            curr = read_int_at(dfd, "charge_now");
          else if ((max = read_int_at(dfd, "energy_full")) > 0)
            curr = read_int_at(dfd, "energy_now");
          if (max > 0 && curr >= 0) cap = 100 * curr / max;
        }
        if (cap >= 0) printf("Battery %d: %d%%\n", TT.bat++, cap);
      }
    } else if (toys.optflags & FLAG_a) {
      if ((on = read_int_at(dfd, "online")) >= 0)
        printf("Adapter %d: %s-line\n", TT.ac++, (on ? "on" : "off"));
    }
done:
    close(dfd);
  }
  free(TT.cpath);
  return 0;
}

int temp_callback(struct dirtree *tree)
{
  int dfd, temp;

  if (tree->name[0]=='.') return 0;
  if (!tree->parent || !tree->parent->parent)
    return DIRTREE_RECURSE|DIRTREE_SYMFOLLOW;
  errno = 0;

  if (0 <= (dfd = open((TT.cpath=dirtree_path(tree, NULL)), O_RDONLY))) {
    if ((0 < (temp = read_int_at(dfd, "temp"))) || !errno) {
      //some tempertures are in milli-C, some in deci-C
      //reputedly some are in deci-K, but I have not seen them
      if (((temp >= 1000) || (temp <= -1000)) && (temp%100 == 0))
        temp /= 100;
      printf("Thermal %d: %d.%d degrees C\n", TT.therm++, temp/10, temp%10);
    }
    close(dfd);
  }
  free(TT.cpath);
  return 0;
}

int cool_callback(struct dirtree *tree)
{
  int dfd=5, cur, max;

  errno = 0;
  memset(toybuf, 0, sizeof(toybuf));

  if (*tree->name == '.') return 0;
  if (!tree->parent) return DIRTREE_RECURSE|DIRTREE_SYMFOLLOW;


  if (0 <= (dfd = open((TT.cpath=dirtree_path(tree, &dfd)), O_RDONLY))) {
    TT.cpath = strcat(TT.cpath, "/type");
    if (readfile(TT.cpath, toybuf, 256) && !errno) {
      toybuf[strlen(toybuf) -1] = 0;
      cur=read_int_at(dfd, "cur_state");
      max=read_int_at(dfd, "max_state");
      if (errno)
        printf("Cooling %d: %s no state information\n", TT.cool++, toybuf);
      else printf("Cooling %d: %s %d of %d\n", TT.cool++, toybuf, cur, max);
    }
    close(dfd);
  }
  free(TT.cpath);
  return 0;
}

void acpi_main(void)
{
  if (toys.optflags & FLAG_V) toys.optflags = FLAG_a|FLAG_b|FLAG_c|FLAG_t;
  if (!toys.optflags) toys.optflags = FLAG_b;
  if (toys.optflags & (FLAG_a|FLAG_b))
    dirtree_read("/sys/class/power_supply", acpi_callback);
  if (toys.optflags & FLAG_t) dirtree_read("/sys/class", temp_callback);
  if (toys.optflags & FLAG_c) dirtree_read("/sys/class/thermal", cool_callback);

}
