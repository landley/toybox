/* acpi.c - show power state
 * Written by Isaac Dunham, 2013
 * No standard.
USE_ACPI(NEWTOY(acpi, "ab", TOYFLAG_USR|TOYFLAG_BIN))

config ACPI
  bool "acpi"
  default n
  help
    usage: acpi [-ab]
    
    Show status of power sources.
    -a   show power adapters
    -b   show batteries
*/

#define FOR_acpi
#include "toys.h"

GLOBALS(
int ac;
int bat;
)

int read_int_at(int dirfd, char *name) {
  int fd, ret=0;
  if ((fd=openat(dirfd, name, O_RDONLY)) < 0)
    return -1;
  FILE * fil = xfdopen(fd, "r");
  fscanf(fil, "%d", &ret);
  fclose(fil);
  return ret;
}

int acpi_callback(struct dirtree *tree)
{
  errno = 0;

  if (tree->name[0]=='.')
    return 0;
  if (strlen(dirtree_path(tree, NULL)) < 26) {
    return (DIRTREE_RECURSE | DIRTREE_SYMFOLLOW);
  }
  int dfd=open(dirtree_path(tree, NULL), O_RDONLY);
  if (dfd > 0) {
    int fd;
    if ((fd = openat(dfd, "type", O_RDONLY)) < 0) {
      close(dfd);
      return 0;
    }
    read(fd, toybuf, 4096);
    close(fd);
    if (0 == strncmp(toybuf, "Battery", 7)) {
      if (toys.optflags & FLAG_b || (!toys.optflags)) {
        int cap = 0, curr = 0, max = 0;
        if ((cap = read_int_at(dfd, "capacity")) < 0) {
          if ((max = read_int_at(dfd, "charge_full")) > 0) {
            curr = read_int_at(dfd, "charge_now");
          } else if ((max = read_int_at(dfd, "energy_full")) > 0) {
            curr = read_int_at(dfd, "energy_now");
          }
          if (max > 0 && (curr >= 0))
            cap = 100 * curr / max;
        }
        if (cap >= 0) printf("Battery %d: %d%%\n", TT.bat++, cap);
      }
    } else {
      //ac
      if (toys.optflags & FLAG_a) {
        int on;
        if ((on = read_int_at(dfd, "online")) >= 0)
          printf("Adapter %d: %s-line\n", TT.ac++, (on ? "on" : "off"));
      }
    }
    close(dfd);
  }
  return 0;
}

void acpi_main(void)
{
  dirtree_read("/sys/class/power_supply", acpi_callback);
}
