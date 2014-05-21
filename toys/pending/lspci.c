/*
 * lspci - written by Isaac Dunham

USE_LSPCI(NEWTOY(lspci, "emkn"USE_LSPCI_TEXT("@i:"), TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default n
  help
    usage: lspci [-ekm]

    List PCI devices.

    -e	Print all 6 digits in class (like elspci)
    -k	Print kernel driver
    -m	Machine parseable format

config LSPCI_TEXT
  bool "lspci readable output"
  depends on LSPCI
  default n
  help
    usage: lspci [-n] [-i /usr/share/misc/pci.ids ]

    -n	Numeric output (repeat for readable and numeric)
    -i	Path to PCI ID database
*/

#define FOR_lspci
#include "toys.h"

GLOBALS(
  char *ids;
  long numeric;

  FILE *db;
)

char *id_check_match(char *id, char *buf)
{
  int i = 0;

  while (id[i]) {
    if (id[i] == buf[i]) i++;
    else return 0;
  }
  return buf + i + 2;
}

/*
 * In: vendid, devid, fil
 * Out: vname, devname
 * Out must be zeroed before use.
 * vmame and devname must be char[256], zeroed out
 * Returns (2 - number of IDs matched): vendor must be matched for 
 * dev to be matched
 */
int find_in_db(char *vendid, char *devid, FILE *fil, char *vname, char *devname)
{
  char buf[256], *vtext = 0L, *dtext = 0L;

  fseek(fil, 0, SEEK_SET);
  while (!*vname) {
    //loop through
    if (!fgets(buf, 255, fil)) return 2;
    if ((vtext = id_check_match(vendid, buf)))
      strncpy(vname, vtext, strlen(vtext) - 1);
  }
  while (!*devname) {
    if (!fgets(buf, 255, fil) || *buf != '\t') return 1;
    if ((dtext = id_check_match(devid, buf + 1)))
      strncpy(devname, dtext, strlen(dtext) - 1);
  }

  // matched both
  return 0;
}

int do_lspci(struct dirtree *new)
{
  int alen = 8, dirfd, res = 2; //no textual descriptions read
  char *dname = dirtree_path(new, &alen);

  memset(toybuf, 0, 4096);
  struct {
    char class[16], vendor[16], device[16], module[256],
    vname[256], devname[256];
  } *bufs = (void*)(toybuf + 2);

  if (!new->parent) return DIRTREE_RECURSE;
  errno = 0;
  dirfd = open(dname, O_RDONLY);
  if (dirfd > 0) {
    char *p, **fields = (char*[]){"class", "vendor", "device", ""};

    for (p = toybuf; **fields; p+=16, fields++) {
      int fd, size = ((toys.optflags & FLAG_e) && p == toybuf) ? 8 : 6;

      if ((fd = openat(dirfd, *fields, O_RDONLY)) < 0) continue;
      xread(fd, p, size);
      close(fd);

      p[size] = 0;
    }

    close(dirfd);
    if (errno) return 0;

    {
      char *driver = "";
      char *fmt = (toys.optflags & FLAG_m) ? "%s, \"%s\" \"%s\" \"%s\" \"%s\"\n"
                                                   : "%s Class %s: %s:%s %s\n";

      if (toys.optflags & FLAG_k) {
        strcat(dname, "/driver");
        if (readlink(dname, bufs->module, sizeof(bufs->module)) != -1)
          driver = basename(bufs->module);
      }
      if (CFG_LSPCI_TEXT && TT.numeric != 1) {
        res = find_in_db(bufs->vendor, bufs->device, TT.db,
                         bufs->vname, bufs->devname);
      }
      if (CFG_LSPCI_TEXT && TT.numeric > 1) {
        fmt = (toys.optflags & FLAG_m)
            ? "%s, \"%s\" \"%s [%s]\" \"%s [%s]\" \"%s\"\n"
            : "%s Class %s: %s [%s] %s [%s] %s\n";
        printf(fmt, new->name + 5, bufs->class, bufs->vname, bufs->vendor,
               bufs->devname, bufs->device, driver);
      } else {
        printf(fmt, new->name + 5, bufs->class, 
               (res < 2) ? bufs->vname : bufs->vendor, 
               !(res) ? bufs->devname : bufs->device, driver);
      }
    }
  }

  return 0;
}

void lspci_main(void)
{
  if (CFG_LSPCI_TEXT && (TT.numeric != 1)) {
    TT.db = fopen(TT.ids ? TT.ids : "/usr/share/misc/pci.ids", "r");
    if (errno) {
      TT.numeric = 1;
      error_msg("could not open PCI ID db");
    }
  }

  dirtree_read("/sys/bus/pci/devices", do_lspci);
}
