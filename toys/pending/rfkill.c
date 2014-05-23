/* rfkill.c - Enable/disable wireless devices.
 *
 * Copyright 2014 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_RFKILL(NEWTOY(rfkill, "<1>2", TOYFLAG_SBIN))

config RFKILL
  bool "rfkill"
  default n
  help
    Usage: rfkill COMMAND

    Enable/disable wireless devices.

    Commands:
    list [IDENTIFIER] List current state
    block IDENTIFIER Disable device
    unblock IDENTIFIER Enable device

    where IDENTIFIER is the index no. of an rfkill switch or one of:
    <idx> all, wlan(wifi), bluetooth, uwb(ultrawideband), wimax, wwan, gps, fm.
*/
#define FOR_rfkill
#include "toys.h"
#include <linux/rfkill.h>

struct arglist {
  char *name;
  int idx;
};

static int getidx(char ***argv, struct arglist *list)
{
  struct arglist *alist;

  if (!**argv) return -1;
  for (alist = list; alist->name; alist++)
    if (!strcmp(**argv, alist->name)) {
      *argv += 1;
      return alist->idx;
    }
  return -1;
}

void rfkill_main(void)
{
  struct rfkill_event rfevent;
  int fd, tvar, idx = -1, tid = RFKILL_TYPE_ALL;
  char **optargs = toys.optargs;
  struct arglist cmds[] = {{"list", 0}, {"block", 1},
                          {"unblock", 2}, {NULL, -1}};

  if (((tvar = getidx(&optargs, cmds)) < 0) || ((tvar) && !*optargs)) {
    toys.exithelp = 1;
    (tvar < 0) ? error_exit("cmd missmatch")
            : error_exit("%s idx missing", *toys.optargs);
  }
  if (*optargs) {
    struct arglist rftypes[] = {{"all", RFKILL_TYPE_ALL},
      {"wifi", RFKILL_TYPE_WLAN}, {"wlan", RFKILL_TYPE_WLAN},
      {"bluetooth", RFKILL_TYPE_BLUETOOTH}, {"uwb", RFKILL_TYPE_UWB},
      {"ultrawideband", RFKILL_TYPE_UWB}, {"wimax", RFKILL_TYPE_WIMAX},
      {"wwan", RFKILL_TYPE_WWAN}, {"gps", RFKILL_TYPE_GPS}, 
      {"fm", 7}, {NULL, -1}}; // RFKILL_TYPE_FM = 7

    if ((tid = getidx(&optargs, rftypes)) == -1)
      idx = atolx_range(*optargs, 0, INT_MAX);
  }
  fd = xcreate("/dev/rfkill", (tvar ? O_RDWR : O_RDONLY)|O_NONBLOCK, 0600);
  if (tvar) {
    memset(&rfevent, 0, sizeof(rfevent));
    rfevent.soft = (tvar & 1);
    if (tid != -1) {
      rfevent.type = tid;
      rfevent.op = RFKILL_OP_CHANGE_ALL;
    } else if (idx >= 0) {
      rfevent.idx = idx;
      rfevent.op = RFKILL_OP_CHANGE;
    }
    xwrite(fd, &rfevent, sizeof(rfevent));
  } else { // show list.
    while (sizeof(rfevent) == readall(fd, &rfevent, sizeof(rfevent))) {
      char *line = NULL, *name = NULL, *type = NULL;

      // filter of list items.
      if (((tid > 0) && (tid != rfevent.type)) 
          || ((idx != -1) && (idx != rfevent.idx))) continue;
      sprintf(toybuf, "/sys/class/rfkill/rfkill%u/uevent", rfevent.idx);
      tvar = xopen(toybuf, O_RDONLY);
      while ((line = get_line(tvar))) {
        if (!strncmp(line, "RFKILL_NAME", strlen("RFKILL_NAME"))) 
          name = xstrdup(strchr(line, '=')+1);
        else if (!strncmp(line, "RFKILL_TYPE", strlen("RFKILL_TYPE")))
          type = xstrdup(strchr(line, '=')+1);
        free(line);
      }
      xprintf("%u: %s: %s\n", rfevent.idx, name, type);
      xprintf("\tSoft blocked: %s\n", rfevent.soft ? "yes" : "no");
      xprintf("\tHard blocked: %s\n", rfevent.hard ? "yes" : "no");
      xclose(tvar);
      free(name), free(type);
    }
  }
  xclose(fd);
}
