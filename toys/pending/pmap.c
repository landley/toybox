/* pmap.c - Reports the memory map of a process or processes.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.
 *
USE_PMAP(NEWTOY(pmap, "xq", TOYFLAG_BIN))

config PMAP
  bool "pmap"
  default y
  help
    usage: pmap [-xq] [pids...]

    Reports the memory map of a process or processes.

    -x Show the extended format.
    -q Do not display some header/footer lines.
*/
#define FOR_pmap
#include "toys.h"

#if ULONG_MAX == 0xffffffff
 # define TAB "\t"
 # define WIDTH "8"
 # define DASHES ""
#else
 # define TAB "\t\t"
 # define WIDTH "16"
 # define DASHES "--------"
#endif

struct _smaps {
  unsigned long start_addr, size, pss, pdirty, swap;
  char mode[5], *mapping;
};

//Display mapping info.
static void show_maps(struct _smaps *map)
{
  xprintf("%0" WIDTH "lx ", map->start_addr);
  if (toys.optflags & FLAG_x)
    xprintf("%7lu %7lu %7lu %7lu ", map->size, map->pss, map->pdirty, map->swap);
  else xprintf("%7luK", map->size);
  xprintf(" %.4s  \n", map->mode, map->mapping);
  free(map->mapping);
}

//Read "/proc/pid/smaps" file and extract data.
static int read_smaps(pid_t pid, struct _smaps *total)
{
  struct _smaps curmap;
  char *line;
  int fd, nitems;
  
  snprintf(toybuf, sizeof(toybuf), "/proc/%u/smaps", pid);
  if ((fd = open(toybuf, O_RDONLY)) < 0) return fd;
  memset(&curmap, 0, sizeof(struct _smaps)); 
  while ((line = get_line(fd))) {
    char *ptr = NULL;
    *toybuf = *(toybuf+34) = *(toybuf+40) = '\0';
    //1st line format -> start_addr-End_addr rw-s ADR M:m OFS
    if ((ptr = strchr(line, '-'))) {
      if (curmap.size) show_maps(&curmap);
      memset(&curmap, 0, sizeof(struct _smaps));
      nitems = sscanf(line, "%s %s %*s %*s %*s %s\n", toybuf, toybuf+34, toybuf+40);
      if (nitems >= 2) {
        ptr = strchr(toybuf, '-');
        *ptr = '\0';
        total->size += curmap.size = (strtoul(++ptr, NULL, 16) - 
            (curmap.start_addr = strtoul(toybuf, NULL, 16))) >> 10;
        strncpy(curmap.mode, toybuf+34, sizeof(curmap.mode)-1);
        if (!*(toybuf+40)) curmap.mapping = xstrdup("  [ anon ]");
        else curmap.mapping = xstrdup(toybuf+40);
      }
    } else { //2nd line onwards..
      unsigned long val = 0;
      nitems = sscanf(line, "%s %lu\n", toybuf, &val);
      if (nitems == 2) {
        if (!strcmp("Pss:", toybuf)) total->pss += (curmap.pss = val);
        else if (!strcmp("Private_Dirty:", toybuf)) total->pdirty += (curmap.pdirty = val);
        else if (!strcmp("Swap:", toybuf)) total->swap += (curmap.swap = val);
      }
    }
    free(line);
  }
  if (curmap.size) show_maps(&curmap);
  xclose(fd);
  return 0;
}

void pmap_main(void)
{
  struct _smaps total;
  int fd;
  
  while (*toys.optargs) {
    pid_t pid = get_int_value(*toys.optargs++, 0, INT_MAX);
    snprintf(toybuf, sizeof(toybuf), "/proc/%u/cmdline", pid);
    if ((fd = open(toybuf, O_RDONLY)) == -1) xprintf("%u: [no such process]\n", pid);
    else {
      ssize_t len = readall(fd, toybuf, sizeof(toybuf) -1);
      if (len <= 0) xprintf("%u: [no such process]\n", (int)pid);
      else {
        toybuf[len] = '\0';
        while (--len >= 0 && toybuf[len] == '\0') continue;
        for (; len > 0; len--)
          if ((unsigned char)toybuf[len] < ' ') toybuf[len] = ' ';
        xprintf("%u: %s\n", (int)pid, toybuf);
      }
      xclose(fd);
    }
    if (!(toys.optflags & FLAG_q) && (toys.optflags & FLAG_x))
      xprintf("Address" TAB "  Kbytes     PSS   Dirty    Swap  Mode  Mapping\n");

    memset(&total, 0, sizeof(struct _smaps));
    if (read_smaps(pid, &total)) toys.exitval = 42;
    else { //to display total mapping.
      if (!(toys.optflags & FLAG_q) )
        (toys.optflags & FLAG_x)
          ? xprintf("--------" DASHES "  ------  ------  ------  ------\n"
              "total" TAB " %7lu %7lu %7lu %7lu\n",
              total.size, total.pss, total.pdirty, total.swap)
          : xprintf("mapped: %luK\n", total.size);
    }
  }
}
