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

struct ids {
  struct ids *next, *child;
  int id;
  char name[];
};

static int list_device(struct dirtree *new)
{
  FILE *file;
  char *name;
  struct ids *ids;
  int busnum = 0, devnum = 0, pid = 0, vid = 0, count = 0;

  if (!new->parent) return DIRTREE_RECURSE;
  if (new->name[0] == '.') return 0;

  // Read data from proc file
  sprintf(toybuf, "%s/uevent", name = dirtree_path(new, 0));
  if (!(file = fopen(toybuf, "r"))) return 0;
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
  free(name);

  return 0;
}

void lsusb_main(void)
{
  char *path = "/etc:/vendor:/usr/share/misc";
  struct string_list *sl;
  int fd = -1;

  // Parse  http://www.linux-usb.org/usb.ids file (if available)
  if ((sl = find_in_path(path, "usb.ids.gz"))) {
    signal(SIGCHLD, SIG_IGN);
    xpopen((char *[]){"zcat", sl->str, 0}, &fd, 1);
  } else if ((sl = find_in_path(path, "usb.ids"))) fd = xopen(sl->str,O_RDONLY);
  if (fd != -1) {
    FILE *fp = fdopen(fd, "r");
    char *s, *ss;
    struct ids *ids, *tids;

    free(sl);
    while ((s = xgetline(fp))) {
      fd = estrtol(s, &ss, 16);
      if (ss == s+4+(*s=='\t') && *ss++==' ') {
        ids = xmalloc(sizeof(*ids)+strlen(ss)+1);
        ids->child = 0;
        ids->id = fd;
        strcpy(ids->name, ss);
        if (!TT.ids || *s!='\t') {
          ids->next = TT.ids;
          TT.ids = ids;
        } else {
          tids = TT.ids;
          ids->next = tids->child;
          tids->child = ids;
        }
      }
      free(s);
    }
    fclose(fp);
  }
  dirtree_read("/sys/bus/usb/devices/", list_device);
}
