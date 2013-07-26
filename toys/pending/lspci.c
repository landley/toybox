/*
 * lspci - written by Isaac Dunham

USE_LSPCI(NEWTOY(lspci, "emkn", TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default n
  help
    usage: lspci [-ekmn] 

    List PCI devices.
    -e  Output all 6 digits in class (like elspci)
    -k  Print kernel driver
    -m  Machine parseable format
    -n  Numeric output (default)
*/
#define FOR_lspci
#include "toys.h"
char * preadat_name(int dirfd, char *fname, size_t nbyte, off_t offset)
{
  int fd;
  char *buf = malloc(nbyte+1);
  memset(buf, 0, sizeof(buf));
  fd = openat(dirfd, fname, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }
  lseek(fd, offset, SEEK_SET);
  read(fd, buf, nbyte);
  close(fd);
  buf[nbyte +1] = '\0';
  return buf;
}

int do_lspci(struct dirtree *new)
{
  int alen = 8;
  char *dname = dirtree_path(new, &alen);
  errno = 0;
  int dirfd = open(dname, O_RDONLY);
  if (dirfd > 0) {
    char *class = preadat_name(dirfd, "class",
                (toys.optflags & FLAG_e) ? 6 :4, 2);
    char *vendor = preadat_name(dirfd, "vendor", 4, 2);
    char *device = preadat_name(dirfd, "device", 4, 2);
    close(dirfd);
    if (!errno) {
      char *driver = "";
      if (toys.optflags & FLAG_k) {
        char module[256] = "";
        strcat(dname, "/driver");
        readlink(dname, module, sizeof(module));
        driver = basename(module);
      }
      if (toys.optflags & FLAG_m) {
        printf("%s, \"%s\" \"%s\" \"%s\" \"%s\"\n",new->name + 5, class, 
               vendor, device, driver);
      } else {
        printf("%s Class %s: %s:%s %s\n", new->name + 5, class, vendor, device, 
               driver);
      }
    }
  }
  if (!strcmp("/sys/bus/pci/devices", new->name)) {
    return DIRTREE_RECURSE;
  }
  return 0;
}

void lspci_main(void)
{
  sprintf(toybuf, "/sys/bus/pci/devices");
  dirtree_read(toybuf, do_lspci);
}
