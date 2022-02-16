/* lsusb.c - list available USB devices
 *
 * Copyright 2013 Andre Renaud <andre@bluewatersys.com>

USE_LSUSB(NEWTOY(lsusb, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config LSUSB
  bool "lsusb"
  default y
  help
    usage: lsusb

    List USB hosts/devices.
*/

#define FOR_lsusb
#include "toys.h"

GLOBALS(
  void *ids;
)

struct dev_ids {
  struct dev_ids *next, *child;
  int id;
  char name[];
};

static int list_device(struct dirtree *new)
{
  FILE *file;
  char *name;
  struct dev_ids *ids;
  int busnum = 0, devnum = 0, pid = 0, vid = 0, count = 0;

  if (!new->parent) return DIRTREE_RECURSE;
  if (new->name[0] == '.') return 0;

  // Read data from proc file
  sprintf(toybuf, "%s/uevent", name = dirtree_path(new, 0));
  if (!(file = fopen(toybuf, "r"))) return 0;
  free(name);
  while (fgets(toybuf, sizeof(toybuf), file))
    if (sscanf(toybuf, "BUSNUM=%u\n", &busnum)
        || sscanf(toybuf, "DEVNUM=%u\n", &devnum)
        || sscanf(toybuf, "PRODUCT=%x/%x/", &pid, &vid)) count++;
  fclose(file);

  // Output with any matching ids data
  if (count == 3) {
    printf("Bus %03d Device %03d: ID %04x:%04x", busnum, devnum, pid, vid);
    for (ids = TT.ids; ids; ids = ids->next) {
      if (pid != ids->id) continue;
      printf("%s", ids->name);
      for (ids = ids->child; ids; ids = ids->next) {
        if (vid != ids->id) continue;
        printf("%s", ids->name);
        break;
      }
      break;
    }
    xputc('\n');
  }

  return 0;
}

// Search for pci.ids or usb.ids and return parsed structure, or NULL
struct dev_ids *parse_dev_ids(char *name)
{
  char *path = "/etc:/vendor:/usr/share/misc";
  struct string_list *sl;
  FILE *fp;
  char *s, *ss;
  struct dev_ids *ids = 0, *new;
  int fd = -1;

  // Open compressed or uncompressed file
  sprintf(toybuf, "%s.gz", name);
  if ((sl = find_in_path(path, toybuf))) {
    signal(SIGCHLD, SIG_IGN);
    xpopen((char *[]){"zcat", sl->str, 0}, &fd, 1);
  } else if ((sl = find_in_path(path, name))) fd = xopen(sl->str,O_RDONLY);
  free(sl);
  if (fd == -1) return 0;

  for (fp = fdopen(fd, "r"); (s = ss = xgetline(fp)); free(s)) {
    if (s[strspn(s, " \t")]=='#' || strstart(&ss, "\t\t")) continue;
    fd = estrtol(s, &ss, 16);
    if (ss == s+4+(*s=='\t') && *ss++==' ') {
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

void lsusb_main(void)
{
  // Parse  http://www.linux-usb.org/usb.ids file (if available)
  TT.ids = parse_dev_ids("usb.ids");
  dirtree_read("/sys/bus/usb/devices/", list_device);
}
