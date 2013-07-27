/*
 * lspci - written by Isaac Dunham

USE_LSPCI(NEWTOY(lspci, "emkns:", TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default n
  help
    usage: lspci [-ekmn]

    List PCI devices.
    -e  Print all 6 digits in class (like elspci)
    -k  Print kernel driver
    -m  Machine parseable format
    -n  Numeric output (default)
*/
#define FOR_lspci
#include "toys.h"

int do_lspci(struct dirtree *new)
{
  int alen = 8, dirfd;
  char *dname = dirtree_path(new, &alen);
  struct {
    char class[16], vendor[16], device[16], module[256];
  } *bufs = (void*)(toybuf + 2);

  if (!strcmp("/sys/bus/pci/devices", dname)) return DIRTREE_RECURSE;
  errno = 0;
  dirfd = open(dname, O_RDONLY);
  if (dirfd > 0) {
    char *p, **fields = (char*[]){"class", "vendor", "device", ""};

    for (p = toybuf; **fields; p+=16, fields++) {
      int fd, size;

      if ((fd = openat(dirfd, *fields, O_RDONLY)) < 0) continue;
      size = 6 + 2*((toys.optflags & FLAG_e) && (p != toybuf));
      p[read(fd, p, size)] = '\0';
      close(fd);
    }

    close(dirfd);
    if (!errno) {
      char *driver = "";
      char *fmt = toys.optflags & FLAG_m ? "%s, \"%s\" \"%s\" \"%s\" \"%s\"\n"
                                                   : "%s Class %s: %s:%s %s\n";

      if (toys.optflags & FLAG_k) {
        strcat(dname, "/driver");
        if (readlink(dname, bufs->module, sizeof(bufs->module)) != -1)
          driver = basename(bufs->module);
      }
      printf(fmt, new->name + 5, bufs->class, bufs->vendor, bufs->device, 
               driver);
    }
  }
  return 0;
}

void lspci_main(void)
{
  dirtree_read("/sys/bus/pci/devices", do_lspci);
}
