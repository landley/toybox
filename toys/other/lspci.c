/*
 * lspci - written by Isaac Dunham

USE_LSPCI(NEWTOY(lspci, "emkn"USE_LSPCI_TEXT("@i:"), TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default y
  help
    usage: lspci [-ekm]

    List PCI devices.

    -e	Print all 6 digits in class
    -k	Print kernel driver
    -m	Machine parseable format

config LSPCI_TEXT
  bool "lspci readable output"
  depends on LSPCI
  default y
  help
    usage: lspci [-n] [-i FILE ]

    -n	Numeric output (repeat for readable and numeric)
    -i	PCI ID database (default /usr/share/misc/pci.ids)

*/

#define FOR_lspci
#include "toys.h"

GLOBALS(
  char *ids;
  long numeric;

  FILE *db;
)

int do_lspci(struct dirtree *new)
{
  char *p = toybuf, *vendor = toybuf+9, *device = toybuf+18,
       driver[256], *vbig = 0, *dbig = 0, **fields;
  int dirfd;

  if (!new->parent) return DIRTREE_RECURSE;

  // Parse data out of /proc

  if (-1 == (dirfd = openat(dirtree_parentfd(new), new->name, O_RDONLY)))
    return 0;

  // it's ok for the driver link not to be there, whatever fortify says
  *driver = 0;
  if (toys.optflags & FLAG_k)
    if (readlinkat(dirfd, "driver", driver, sizeof(driver))) {};

  for (fields = (char*[]){"class", "vendor", "device", 0}; *fields; fields++) {
    int fd, size = 6 + 2*((toys.optflags & FLAG_e) && p == toybuf);
    *p = 0;

    if (-1 == (fd = openat(dirfd, *fields, O_RDONLY))) {
      close(dirfd);
      return 0;
    }
    xreadall(fd, p, size);
    memmove(p, p+2, size -= 2);
    p[size] = 0;
    close(fd);
    p += 9;
  }

  close(dirfd);

  // Lookup/display data from pci.ids?

  if (CFG_LSPCI_TEXT && TT.db) {
    if (TT.numeric != 1) {
      char *s;

      fseek(TT.db, 0, SEEK_SET);
      while (!vbig || !dbig) {
        s = p;
        if (!fgets(s, sizeof(toybuf)-(p-toybuf)-1, TT.db)) break;
        while (isspace(*s)) s++;
        if (*s == '#') continue;
        if (vbig && s == p) break;
        if (strstart(&s, vbig ? device : vendor)) {
          if (vbig) dbig = s+2;
          else vbig = s+2;
          s += strlen(s);
          s[-1] = 0; // trim ending newline
          p = s + 1;
        }
      }
    }

    if (TT.numeric > 1) {
      printf((toys.optflags & FLAG_m)
        ? "%s, \"%s\" \"%s [%s]\" \"%s [%s]\""
        : "%s Class %s: %s [%s] %s [%s]",
        new->name+5, toybuf, vbig ? vbig : "", vendor,
        dbig ? dbig : "", device);

      goto driver;
    }
  }

  printf((toys.optflags & FLAG_m) ? "%s \"%s\" \"%s\" \"%s\""
    : "%s Class %s: %s:%s", new->name+5, toybuf, 
    vbig ? vbig : vendor, dbig ? dbig : device);

driver:
  if (*driver)
    printf((toys.optflags & FLAG_m) ? " \"%s\"" : " %s", basename(driver));
  xputc('\n');

  return 0;
}

void lspci_main(void)
{
  if (CFG_LSPCI_TEXT && TT.numeric != 1) {
    if (!TT.ids) TT.ids = "/usr/share/misc/pci.ids";
    if (!(TT.db = fopen(TT.ids, "r")))
      perror_msg("could not open PCI ID db");
  }

  dirtree_read("/sys/bus/pci/devices", do_lspci);
}
