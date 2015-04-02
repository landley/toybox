/* ps.c - show process list
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html
 * And http://kernel.org/doc/Documentation/filesystems/proc.txt Table 1-4
 * And linux kernel source fs/proc/array.c function do_task_stat()
 *
 * Deviation from posix: no -n

USE_PS(NEWTOY(ps, "aAdeo*", TOYFLAG_USR|TOYFLAG_BIN))

config PS
  bool "ps"
  default n
  help
    usage: ps [-Aade] [-fl] [-gG GROUP] [-o FIELD] [-p PID] [-t TTY] [-u USER]

    List processes.

    -A	All processes
    -a	Processes with terminals, except session leaders
    -d	Processes that aren't session leaders
    -e	Same as -A


    -f	Full listing
    -g  Processes belonging to these session leaders
    -G	Processes with these real group IDs
    -l	Long listing
    -o	Show FIELDS for each process
    -p	select by PID
    -t	select by TTY
    -u	select by USER
    -U	select by USER

     GROUP, FIELD, PID, TTY, and USER are comma separated lists.

    OUTPUT (-o) FIELDS:

      S Linux defines the process state letters as:
        R (running) S (sleeping) D (disk sleep) T (stopped)  t (tracing stop)
        Z (zombie)  X (dead)     x (dead)       K (wakekill) W (waking)
      F Process flags (PF_*) from linux source file include/sched.h
        (in octal rather than hex because posix inexplicably says so)

    Default is PID,TTY,TIME,CMD  With -f UID,PID,PPID,C,STIME,TTY,TIME,CMD
    With -l F,S,UID,PID,PPID,C,PRI,NI,ADDR,SZ,WCHAN,TTY,TIME,CMD
*/

#define FOR_ps
#include "toys.h"

GLOBALS(
  struct arg_list *o;

  unsigned width;
  dev_t tty;
  void *oo;
)

/*
  l l fl  a   fl   fl l  l  l    l  l     f     a   a    a
  F S UID PID PPID C PRI NI ADDR SZ WCHAN STIME TTY TIME CMD
  ruser user rgroup group pid ppid pgid pcpu vsz nice etime time tty comm args

  todo: thread support /proc/$d/task/%d/stat
    man page: F flags mean...
*/

// dirtree callback.
// toybuf used as: 1024 /proc/$PID/stat, 1024 slot[], 2048 /proc/$PID/cmdline
static int do_ps(struct dirtree *new)
{
  long long *slot = (void *)(toybuf+1024);
  char *name, *s, state;
  int nlen, slots, i, field, width = TT.width, idxes[] = {0, -1, -2, -3};
  struct passwd *pw;
  struct group *gr;

width=18;

  if (!new->parent) return DIRTREE_RECURSE;
  if (!(*slot = atol(new->name))) return 0;

  // name field limited to 256 bytes by VFS, plus 40 fields * max 20 chars:
  // 1000-ish total, but some forced zero so actually there's headroom.
  sprintf(toybuf, "%lld/stat", *slot);
  if (!readfileat(dirtree_parentfd(new), toybuf, toybuf, 1024)) return 0;

  // parse oddball fields (name and state)
  if (!(s = strchr(toybuf, '('))) return 0;
  for (name = ++s; *s != ')'; s++) if (!*s) return 0;
  nlen = s++-name;
  if (1>sscanf(++s, " %c%n", &state, &i)) return 0;

  // parse numeric fields
  for (slots = 1; slots<100; slots++)
    if (1>sscanf(s += i, " %lld%n", slot+slots, &i)) break;

  // skip entries we don't care about.
  if ((toys.optflags&(FLAG_a|FLAG_d)) && getsid(*slot)==*slot) return 0;
  if ((toys.optflags&FLAG_a) && !slot[4]) return 0;
  if (!(toys.optflags*(FLAG_a|FLAG_d|FLAG_A|FLAG_e)) && TT.tty!=slot[4])
    return 0;

  // default: pidi=1 user time args

  for (field = 0; field<ARRAY_LEN(idxes); field++) {
    char *out = toybuf+2048;
    int idx = idxes[field];

    // Default: unsupported
    sprintf(out, "-");

    // Our tests here are in octal to match \123 character escapes in typos[]

    // Print a raw stat field?
    if (idx<0300)
      sprintf(out, (char *[]){"%lld","%llx","%llo"}[idx>>6], slot[idx&63]);

    else if (idx == 0300) sprintf(out, "%c", state); // S
    else if (idx == 0301 || idx == 0302) {           // UID and USER
      sprintf(out, "%d", new->st.st_uid);
      if (idx == 302 || (toys.optflags&FLAG_f)) {
        struct passwd *pw = getpwuid(new->st.st_uid);

        if (pw) out = pw->pw_name;
      }
    } else if (idx == 0303);                         // C (unsupported for now)
    else if (idx == 0304)                            // SZ
      sprintf(out, "%lld", slot[22]/4096);
    else if (idx == 0305) {                          // WCHAN
      sprintf(toybuf+512, "%lld/wchan");
      readfileat(dirtree_parentfd(new), toybuf+512, out, 2047);

    // time
    } else if (idx == -2) {
      long seconds = (slot[11]+(idx==-3)*slot[12])/sysconf(_SC_CLK_TCK),
           ll = 60*60*24;

      for (s = out, i = 0; i<4; i++) {
        if (i>1 || seconds > ll)
          s += sprintf(s, "%*ld%c", 2*(i==3), seconds/ll, "-::"[i]);
        ll /= i ? 60 : 24;
      }

    // Command line limited to 2k displayable. We could dynamically malloc, but
    // it'd almost never get used, querying length of a proc file is awkward,
    // fixed buffer is nommu friendly... Waiting for somebody to complain. :)
    } else if (idx == -3) {
      int fd, len = 0;

      sprintf(out, "%lld/cmdline", *slot);
      fd = openat(dirtree_parentfd(new), out, O_RDONLY);
 
      if (fd != -1) {
        if (0<(len = read(fd, out, 2047))) {
          out[len] = 0;
          for (i = 0; i<len; i++) if (out[i] < ' ') out[i] = ' ';
        }
        close(fd);
      }
      if (len<1) sprintf(out, "[%.*s]", nlen, name);
    }

    printf(" %*.*s", width, width, out);
  }
  xputc('\n');

  return 0;
}

void ps_main(void)
{
  int i, fd = -1;
  // Octal output code followed by header name
  char *typos[] = {
    "\207F", "\300S", "\301UID", "\0PID", "\02PPID", "\303C", "\20PRI",
    "\21NI", "\34ADDR", "\304SZ", "\305WCHAN", "STIME", "TTY", "TIME", "CMD"
  };

  // l l fl  a   fl   fl l  l  l    l  l     f     a   a    a
  // F S UID PID PPID C PRI NI ADDR SZ WCHAN STIME TTY TIME CMD
  // 7 

  TT.width = 80;
  terminal_size(&TT.width, 0);

  // find controlling tty, falling back to /dev/tty if none
  for (i = fd = 0; i < 4; i++) {
    struct stat st;

    if (i != 3 || -1 != (i = fd = open("/dev/tty", O_RDONLY))) {
      if (isatty(i) && !fstat(i, &st)) {
        TT.tty = st.st_rdev;
        break;
      }
    }
  }
  if (fd != -1) close(fd);

  if (FLAG_o) {
    printf("todo\n");
  } else {
    short def = 0x0807;

    if (toys.optflags&FLAG_f) def = 0x1e0f;
    if (toys.optflags&FLAG_l) def = 0x7ff7;

    // order of fields[] matches posix STDOUT section, so add enabled XSI
    // defaults according to bitmask
  }

  printf("pid user time args\n");
  dirtree_read("/proc", do_ps);
}
