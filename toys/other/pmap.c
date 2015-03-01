/* pmap.c - Reports the memory map of a process or processes.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_PMAP(NEWTOY(pmap, "<1xq", TOYFLAG_BIN))

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

void pmap_main(void)
{
  char **optargs;

  for (optargs = toys.optargs; *optargs; optargs++) {
    pid_t pid = atolx(*optargs);
    FILE *fp;
    char *line, *oldline = 0, *name = 0,
         *k = (toys.optflags & FLAG_x) ? "" : "K";
    size_t len;
    long long start, end, pss, tpss = 0, dirty, tdirty = 0, swap, tswap = 0,
              total = 0;
    int xx = 0;

    snprintf(toybuf, sizeof(toybuf), "/proc/%u/cmdline", pid);
    line = readfile(toybuf, 0, 0);
    if (!line) error_msg("No %lu", (long)pid);
    xprintf("%u: %s\n", (int)pid, line);
    free(line);

    // Header
    // Only use the more verbose file in -x mode
    sprintf(toybuf, "/proc/%u/%smaps", pid,
      (toys.optflags & FLAG_x) ? "s" : "");
    if (!(fp = fopen(toybuf, "r"))) {
      error_msg("No %ld\n", (long)pid);
      return;
    }

    if ((toys.optflags & (FLAG_q|FLAG_x)) == FLAG_x)
      xprintf("Address%*cKbytes     PSS   Dirty    Swap  Mode  Mapping\n",
        (int)(sizeof(long)*2)-4, ' ');

    // Loop through mappings
    for (;;) {
      int off, count;

      line = 0;
      if (0 >= getline(&line, &len, fp)) break;
      count = sscanf(line, "%llx-%llx %s %*s %*s %*s %n",
        &start, &end, toybuf, &off);

      if (count == 3) {
        name = line[off] ? line+off : "  [anon]\n";
        if (toybuf[3] == 'p') toybuf[3] = '-';
        total += end = (end-start)/1024;
        printf("%0*llx % *lld%s ", (int)(2*sizeof(long)), start,
          6+!!(toys.optflags & FLAG_x), end, k);
        if (toys.optflags & FLAG_x) {
          oldline = line;
          continue;
        }
      } else {
        if (0<sscanf(line, "Pss: %lld", &pss)
            || 0<sscanf(line, "Private_Dirty: %lld", &dirty)
            || 0<sscanf(line, "Swap: %lld", &swap)) xx++;
        free(line);
        if (xx<3) continue;
        line = oldline;
        name = basename(name);
        xx = 0;
        printf("% 7lld %7lld %7lld ", pss, dirty, swap);
        tpss += pss;
        tdirty += dirty;
        tswap += swap;
      }

      xprintf("%s-  %s%s", toybuf, line[off]=='[' ? "  " : "", name);

      free(line);
      line = 0;
    }

    // Trailer
    if (!(toys.optflags & FLAG_q)) {
      int x = !!(toys.optflags & FLAG_x);
      if (x) {
        memset(toybuf, '-', 16);
        xprintf("%.*s  ------  ------  ------  ------\n", (int)(sizeof(long)*2),
          toybuf);
      }
      printf("total% *lld%s", 2*(int)(sizeof(long)+1)+x, total, k);
      if (x) printf("% 8lld% 8lld% 8lld", tpss, tdirty, tswap);
      xputc('\n');
    }
 
    fclose(fp);
  }
}
