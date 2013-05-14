/* lsusb.c - list available USB devices
 *
 * Copyright 2013 Andre Renaud <andre@bluewatersys.com>

USE_LSUSB(NEWTOY(lsusb, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config LSUSB
  bool "lsusb"
  default n
  help
    usage: lsusb
*/

#include "toys.h"

static int list_device(struct dirtree *new)
{
  FILE *file;
  char *name;
  int busnum = 0;
  int devnum = 0;
  int pid = 0;
  int vid = 0;
  if (!new->parent)
    return DIRTREE_RECURSE;
  if (new->name[0] == '.')
    return 0;
  name = dirtree_path(new, 0);
  snprintf(toybuf, sizeof(toybuf), "%s/%s", name, "/uevent");
  file = fopen(toybuf, "r");
  if (!file)
    return 0;
  if (!fgets(toybuf, sizeof(toybuf), file) || !strncmp(toybuf, "DEVTYPE=", 8)) {
    fclose(file);
    return 0;
  }
  while (fgets(toybuf, sizeof(toybuf), file)) {
    if (!strncmp(toybuf, "BUSNUM=", 7))
      busnum = atoi(&toybuf[7]);
    if (!strncmp(toybuf, "DEVNUM=", 7))
      devnum = atoi(&toybuf[7]);
    if (!strncmp(toybuf, "PRODUCT=", 8)) {
      char *pos = strchr(toybuf, '/');
      pid = xstrtoul(&toybuf[8], NULL, 16);
      if (pos)
        vid = xstrtoul(pos + 1, NULL, 16);
    }
  }
  fclose(file);

  printf("Bus %03d Device %03d: ID %04x:%04x\n", busnum, devnum, pid, vid);

  return 0;
}

void lsusb_main(void)
{
  dirtree_read("/sys/bus/usb/devices/", list_device);
  return;
}
