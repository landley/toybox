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

#include "toys.h"

static int list_device(struct dirtree *new)
{
  FILE *file;
  char *name;
  int busnum = 0, devnum = 0, pid = 0, vid = 0;

  if (!new->parent) return DIRTREE_RECURSE;
  if (new->name[0] == '.') return 0;
  name = dirtree_path(new, 0);
  sprintf(toybuf, "%s/uevent", name);
  file = fopen(toybuf, "r");
  if (file) {
    int count = 0;

    while (fgets(toybuf, sizeof(toybuf), file))
      if (sscanf(toybuf, "BUSNUM=%u\n", &busnum)
          || sscanf(toybuf, "DEVNUM=%u\n", &devnum)
          || sscanf(toybuf, "PRODUCT=%x/%x/", &pid, &vid)) count++;

    if (count == 3)
      printf("Bus %03d Device %03d: ID %04x:%04x\n", busnum, devnum, pid, vid);
    fclose(file);
  }
  free(name);

  return 0;
}

void lsusb_main(void)
{
  dirtree_read("/sys/bus/usb/devices/", list_device);
}
