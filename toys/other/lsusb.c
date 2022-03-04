/* lsusb.c - list available USB devices
 *
 * Copyright 2013 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2013 Isaac Dunham <ibid.ag@gmail.com>

USE_LSUSB(NEWTOY(lsusb, "i:", TOYFLAG_USR|TOYFLAG_BIN))
USE_LSPCI(NEWTOY(lspci, "emkn@i:", TOYFLAG_USR|TOYFLAG_BIN))

config LSPCI
  bool "lspci"
  default y
  help
    usage: lspci [-ekmn] [-i FILE]

    List PCI devices.

    -e  Extended (6 digit) class
    -i  ID database (default /etc/pci.ids[.gz])
    -k  Show kernel driver
    -m  Machine readable
    -n  Numeric output (-nn for both)

config LSUSB
  bool "lsusb"
  default y
  help
    usage: lsusb [-i]

    List USB hosts/devices.

    -i	ID database (default /etc/usb.ids[.gz])
*/

#define FOR_lsusb
#include "toys.h"

GLOBALS(
  char *i;
  long n;

  void *ids, *class;
  int count;
)

struct dev_ids {
  struct dev_ids *next, *child;
  int id;
  char name[];
};

struct scanloop {
  char *pattern;
  void *d1, *d2;
};

// Common function to read uevent file under /proc for both pci and usb
// note that %s is omitted (because pointer is into toybuf, avoiding copy).
static int scan_uevent(struct dirtree *new, int len, struct scanloop *sl)
{
  int ii, count = 0;
  off_t flen = sizeof(toybuf);
  char *ss, *yy;

  // Read data
  if (*new->name == '.') return 0;
  sprintf(toybuf, "%s/uevent", new->name);
  if (!readfileat(dirtree_parentfd(new), ss = toybuf, toybuf, &flen)) return 0;

  // Loop over lines
  while ((flen = strcspn(ss, "\n"))) {
    if (ss[flen]) ss[flen++] = 0;
    yy = ss+flen;

    // Try each pattern
    for (ii = 0; ii<len; ii++) {
      if (strchr(sl[ii].pattern, '%')) {
        if (2-!sl[ii].d2==sscanf(ss, sl[ii].pattern, sl[ii].d1, sl[ii].d2))
          break;
      } else if (strstart(&ss, sl[ii].pattern)) {
        *(void **)sl[ii].d1 = ss;
        break;
      }
    }
    if (ii!=len) count++;
    ss = yy;
  }

  return count;
}

static void get_names(struct dev_ids *ids, int id1, int id2,
  char **name1, char **name2)
{
  // Look up matching dev_ids (if any)
  *name1 = *name2 = "";
  for (; ids; ids = ids->next) {
    if (id1 != ids->id) continue;
    *name1 = ids->name;
    for (ids = ids->child; ids; ids = ids->next) {
      if (id2 != ids->id) continue;
      *name2 = ids->name;
      return;
    }
    return;
  }
}

// Search for pci.ids or usb.ids and return parsed structure or NULL
struct dev_ids *parse_dev_ids(char *name, struct dev_ids **and)
{
  char *path = "/etc:/vendor:/usr/share/misc";
  struct string_list *sl;
  FILE *fp;
  char *s, *ss, *sss;
  struct dev_ids *ids = 0, *new;
  int fd = -1, tick = 0;

  // Open compressed or uncompressed file
  sprintf(toybuf, "%s.gz", name);
  if ((sl = find_in_path(path, toybuf))) {
    signal(SIGCHLD, SIG_IGN);
    xpopen((char *[]){"zcat", sl->str, 0}, &fd, 1);
  } else if ((sl = find_in_path(path, name))) fd = xopen(sl->str,O_RDONLY);
  llist_traverse(sl, free);
  if (fd == -1) return 0;
  
  for (fp = fdopen(fd, "r"); (s = ss = xgetline(fp)); free(s)) {
    // TODO parse and use third level instead of skipping it here
    if (s[strspn(s, " \t")]=='#' || strstart(&ss, "\t\t")) continue;

    // Switch to device class list?
    if (strstart(&ss, "C ") && and) {
      *and = ids;
      and = 0;
      tick++;
    } 
    fd = estrtol(sss = ss, &ss, 16);
    if (ss>sss && *ss++==' ') {
      while (isspace(*ss)) ss++;
      new = xmalloc(sizeof(*new)+strlen(ss)+1);
      new->child = 0;
      new->id = fd;
      strcpy(new->name, ss);
      if (!ids || *s!='\t') {
        new->next = ids;
        ids = new;
      } else {
        new->next = ids->child;
        ids->child = new;
      }
    }
  }
  fclose(fp);

  return ids;
}

static int list_usb(struct dirtree *new)
{
  int busnum = 0, devnum = 0, pid = 0, vid = 0;
  char *n1, *n2;

  if (!new->parent) return DIRTREE_RECURSE;
  if (3 == scan_uevent(new, 3, (struct scanloop[]){{"BUSNUM=%u", &busnum, 0},
    {"DEVNUM=%u", &devnum, 0}, {"PRODUCT=%x/%x", &pid, &vid}}))
  {
    get_names(TT.ids, pid, vid, &n1, &n2);
    printf("Bus %03d Device %03d: ID %04x:%04x %s %s\n",
      busnum, devnum, pid, vid, n1, n2);
  }

  return 0;
}

void lsusb_main(void)
{
  // Parse http://www.linux-usb.org/usb.ids file (if available)
  TT.ids = parse_dev_ids("usb.ids", 0);
  dirtree_read("/sys/bus/usb/devices/", list_usb);
}

#define FOR_lspci
#include "generated/flags.h"

// TODO: -v
static int list_pci(struct dirtree *new)
{
  char *driver = 0, buf[16], *ss, *names[3];
  int cvd[3] = {0}, ii, revision = 0;
  off_t len = sizeof(toybuf);

// Output formats: -n, -nn, -m, -nm, -nnm, -k

  if (!new->parent) return DIRTREE_RECURSE;
  if (strlen(new->name)<6) return 0;
  TT.count = 0;

  // Load revision
  sprintf(toybuf, "%s/revision", new->name);
  if (readfileat(dirtree_parentfd(new), ss = toybuf, toybuf, &len)) {
    strstart(&ss, "0x");
    sscanf(ss, "%x", &revision);
  }

  // Load uevent data, look up names in database
  if (3 != scan_uevent(new, 3, (struct scanloop[]){{"DRIVER=", &driver, 0},
    {"PCI_CLASS=%x", cvd, 0}, {"PCI_ID=%x:%x", cvd+1, cvd+2}})) return 0;
  get_names(TT.class, 255&(cvd[0]>>16), 255&(cvd[0]>>8), names, names);
  get_names(TT.ids, cvd[1], cvd[2], names+1, names+2);
  if (!FLAG(e)) cvd[0] >>= 8;

  // Output line according to flags
  printf("%s", new->name+5);
  for (ii = 0; ii<3; ii++) {
    sprintf(buf, "%0*x", 6-2*(ii||!FLAG(e)), cvd[ii]);
    if (!TT.n) printf(FLAG(m) ? " \"%s\"" : ": %s"+(ii!=1), names[ii] ? : buf);
    else if (TT.n==1) printf(FLAG(m) ? " \"%s\"" : (ii==2) ? "%s " : " %s:", buf);
    else if (!FLAG(m)) {
      // This one permutes the order, so do it all first time and abort loop
      printf(" %s [%s]: %s %s [%04x:%04x]", names[0], buf, names[1], names[2],
        cvd[1], cvd[2]);
      break;
    } else printf(" \"%s [%s]\"", names[ii], buf);
  }
  printf(FLAG(m) ? " -r%02x" : " (rev %02x)", revision);
  if (FLAG(k)) printf(FLAG(m) ? " \"%s\"" : " %s", driver);
  xputc('\n');

  return 0;
}

void lspci_main(void)
{
  // Parse https://pci-ids.ucw.cz/v2.2/pci.ids (if available)
  if (TT.n != 1) TT.class = parse_dev_ids("pci.ids", (void *)&TT.ids);
  dirtree_read("/sys/bus/pci/devices/", list_pci);
}
