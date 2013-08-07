/*
 * lspci - written by Isaac Dunham

USE_LSPCI(NEWTOY(lspci, "emkn@", TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default n
  help
    usage: lspci [-ekmn@]

    List PCI devices.
    -e  Print all 6 digits in class (like elspci)
    -k  Print kernel driver
    -m  Machine parseable format
    -n  Numeric output

config LSPCI_TEXT
  bool "lspci readable output"
  depends on LSPCI
  default n
  help
    lspci without -n prints readable descriptions;
    lspci -nn prints both readable and numeric description
*/
#define FOR_lspci
#include "toys.h"
extern int find_in_db(char * , char * , FILE * , char * , char * );

GLOBALS(
long numeric;

FILE * db;
)

char * id_check_match(char * id, char * buf)
{
  int i = 0;
  while (id[i]) {
    if (id[i] == buf[i]) {
      i++;
    } else {
      return (char *)0L;
    }
  }
  return (buf + i + 2);
}

/*
 * In: vendid, devid, fil
 * Out: vname, devname
 * Out must be zeroed before use.
 * vmame and devname must be char[256], zeroed out
 * Returns (2 - number of IDs matched): vendor must be matched for 
 * dev to be matched
 */
int find_in_db(char * vendid, char * devid, FILE * fil,
               char * vname, char * devname)
{
  fseek(fil, 0, SEEK_SET);
  char buf[256], *vtext = 0L, *dtext = 0L;
  while (!(vname[0])) {
    //loop through
    if (fgets(buf, 255, fil)==NULL) return 2;
    if ((vtext = id_check_match(vendid, buf)))
      strncpy(vname, vtext, strlen(vtext) - 1);
  }
  while (!(devname[0])) {
    if ((fgets(buf, 255, fil)==NULL) || (buf[0] != '\t' ))
      return 1;
    if ((dtext = id_check_match(devid, buf + 1)))
      strncpy(devname, dtext, strlen(dtext) - 1);
  }
  return 0; /* Succeeded in matching both */
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

  if (!strcmp("/sys/bus/pci/devices", dname)) return DIRTREE_RECURSE;
  errno = 0;
  dirfd = open(dname, O_RDONLY);
  if (dirfd > 0) {
    char *p, **fields = (char*[]){"class", "vendor", "device", ""};

    for (p = toybuf; **fields; p+=16, fields++) {
      int fd, size;

      if ((fd = openat(dirfd, *fields, O_RDONLY)) < 0) continue;
      size = ((toys.optflags & FLAG_e) && (p == toybuf)) ? 8 : 6;
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
      if (CFG_LSPCI_TEXT && (TT.numeric != 1)) {
        res = find_in_db(bufs->vendor, bufs->device, TT.db,
                         bufs->vname, bufs->devname);
      }
      if (CFG_LSPCI_TEXT && (TT.numeric == 2)) {
        fmt = toys.optflags & FLAG_m 
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
    TT.db = fopen("/usr/share/misc/pci.ids", "r");
    if (errno) {
      TT.numeric = 1;
      error_msg("could not open PCI ID db");
    }
  }

  dirtree_read("/sys/bus/pci/devices", do_lspci);
}
