/* ps.c - show process list
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html
 * And http://kernel.org/doc/Documentation/filesystems/proc.txt Table 1-4
 * And linux kernel source fs/proc/array.c function do_task_stat()
 *
 * Deviations from posix: no -n because /proc/self/wchan exists; we use -n to
 * mean "show numeric users and groups" instead.
 * Posix says default output should have field named "TTY" but if you "-o tty"
 * the same field should be called "TT" which is _INSANE_ and I'm not doing it.
 * Similarly -f outputs USER but calls it UID (we call it USER).
 * It also says that -o "args" and "comm" should behave differently but use
 * the same title, which is not the same title as the default output. (No.)
 * Select by session id is -s not -g.
 *
 * Posix defines -o ADDR as "The address of the process" but the process
 * start address is a constant on any elf system with mmu. The procps ADDR
 * field always prints "-" with an alignment of 1, which is why it has 11
 * characters left for "cmd" in in 80 column "ps -l" mode. On x86-64 you
 * need 12 chars, leaving nothing for cmd: I.E. posix 2008 ps -l mode can't
 * be sanely implemented on 64 bit Linux systems. In procps there's ps -y
 * which changes -l by removing the "F" column and swapping RSS for ADDR,
 * leaving 9 chars for cmd, so we're using that as our -l output.
 *
 * Added a bunch of new -o fields posix doesn't mention, and we don't
 * label "ps -o command,args,comm" as "COMMAND COMMAND COMMAND". We don't
 * output argv[0] unmodified for -o comm or -o args (but procps violates
 * posix for -o comm anyway, it's stat[2] not argv[0]).
 *
 * TODO: ps aux (att & bsd style "ps -ax" vs "ps ax" behavior difference)
 * TODO: switch -fl to -y
 * TODO: thread support /proc/$d/task/%d/stat (and -o stat has "l")

USE_PS(NEWTOY(ps, "k(sort)*P(ppid)*aAdeflno*p(pid)*s*t*u*U*g*G*wZ[!ol][+Ae]", TOYFLAG_USR|TOYFLAG_BIN))
USE_TTOP(NEWTOY(ttop, ">0d#=3n#<1mb", TOYFLAG_USR|TOYFLAG_BIN))

config PS
  bool "ps"
  default y
  help
    usage: ps [-AadeflnwZ] [-gG GROUP,] [-k FIELD,] [-o FIELD,] [-p PID,] [-t TTY,] [-uU USER,]

    List processes.

    Which processes to show (selections may be comma separated lists):

    -A	All processes
    -a	Processes with terminals that aren't session leaders
    -d	All processes that aren't session leaders
    -e	Same as -A
    -g	Belonging to GROUPs
    -G	Belonging to real GROUPs (before sgid)
    -p	PIDs (--pid)
    -P	Parent PIDs (--ppid)
    -s	In session IDs
    -t	Attached to selected TTYs
    -u	Owned by USERs
    -U	Owned by real USERs (before suid)

    Output modifiers:

    -k	Sort FIELDs in +increasing or -decreasting order (--sort)
    -n	Show numeric USER and GROUP
    -w	Wide output (don't truncate at terminal width)

    Which FIELDs to show. (Default = -o PID,TTY,TIME,CMD)

    -f	Full listing (-o USER:8=UID,PID,PPID,C,STIME,TTY,TIME,CMD)
    -l	Long listing (-o F,S,UID,PID,PPID,C,PRI,NI,ADDR,SZ,WCHAN,TTY,TIME,CMD)
    -o	Output the listed FIELDs, each with optional :size and/or =title
    -Z	Include LABEL

    Available -o FIELDs:

      ADDR    Instruction pointer
      ARGS    Command line (argv[0-X] minus path)
      CMD     COMM without -f, ARGS with -f
      CMDLINE Command line (argv[0-X])
      COMM    Original command name
      COMMAND Original command path
      CPU     Which processor running on
      ETIME   Elapsed time since process start
      F       Flags (1=FORKNOEXEC, 4=SUPERPRIV)
      GID     Group id
      GROUP   Group name
      LABEL   Security label
      MAJFL   Major page faults
      MINFL   Minor page faults
      NAME    Command name (argv[0])
      NI      Niceness (lower is faster)
      PCPU    Percentage of CPU time used
      PGID    Process Group ID
      PID     Process ID
      PPID    Parent Process ID
      PRI     Priority (higher is faster)
      PSR     Processor last executed on
      RGID    Real (before sgid) group ID
      RGROUP  Real (before sgid) group name
      RSS     Resident Set Size (memory in use)
      RTPRIO  Realtime priority
      RUID    Real (before suid) user ID
      RUSER   Real (before suid) user name
      S       Process state:
              R (running) S (sleeping) D (device I/O) T (stopped)  t (traced)
              Z (zombie)  X (deader)   x (dead)       K (wakekill) W (waking)
      SCHED   Scheduling policy (0=other, 1=fifo, 2=rr, 3=batch, 4=iso, 5=idle)
      STAT    Process state (S) plus:
              < high priority          N low priority L locked memory
              s session leader         + foreground   l multithreaded
      STIME   Start time of process in hh:mm (size :19 shows yyyy-mm-dd hh:mm:ss)
      SZ      Memory Size (4k pages needed to completely swap out process)
      TIME    CPU time consumed
      TTY     Controlling terminal
      UID     User id
      USER    User name
      VSZ     Virtual memory size (1k units)
      WCHAN   Waiting in kernel for

config TTOP
  bool "ttop"
  default n
  help

    usage: ttop [-mb] [ -d seconds ] [ -n iterations ]

    Provide a view of process activity in real time.
    Keys
       N/M/P/T show CPU usage, sort by pid/mem/cpu/time
       S       show memory
       R       reverse sort
       H       toggle threads
       C,1     toggle SMP
       Q,^C    exit

    Options
       -n Iterations before exiting
       -d Delay between updates
       -m Same as 's' key
       -b Batch mode
*/

#define FOR_ps
#include "toys.h"

GLOBALS(
  union {
    struct {
      struct arg_list *G;
      struct arg_list *g;
      struct arg_list *U;
      struct arg_list *u;
      struct arg_list *t;
      struct arg_list *s;
      struct arg_list *p;
      struct arg_list *o;
      struct arg_list *P;
      struct arg_list *k;
    } ps;
    struct {
      long n;
      long d;
    } ttop;
  };

  struct sysinfo si;
  struct ptr_len gg, GG, pp, PP, ss, tt, uu, UU;
  unsigned width;
  dev_t tty;
  void *fields, *kfields;
  long long ticks, bits;
  size_t header_len;
  int kcount;
)

struct strawberry {
  struct strawberry *next, *prev;
  short which, len, reverse;
  char *title;
  char forever[];
};

/* The slot[] array is mostly populated from /proc/$PID/stat (kernel proc.txt
 * table 1-4) but we shift and repurpose fields, with the result being:
 *
 * 0  pid         process id                1  ppid        parent process id
 * 2  pgrp        process group             3  sid         session id
 * 4  tty_nr      tty the process uses      5  tty_pgrp    pgrp of the tty
 * 6  flags       task flags                7  min_flt     minor faults
 * 8  cmin_flt    minor faults+child        9  maj_flt     major faults
 * 10 cmaj_flt    major faults+child        11 utime       user+kernel jiffies
 * 12 stime       kernel mode jiffies       13 cutime      user jiffies+child
 * 14 cstime      kernel mode jiffies+child 15 priority    priority level
 * 16 nice        nice level                17 num_threads number of threads
 * 18 vmlck       locked memory             19 start_time  jiffies after boot
 * 20 vsize       virtual memory size       21 rss         resident set size
 * 22 rsslim      limit in bytes on rss     23 start_code  code segment addr
 * 24 end_code    code segment address      25 start_stack stack address
 * 26 esp         current value of ESP      27 eip         current value of EIP
 * 28 pending     bitmap of pending signals 29 blocked     blocked signal bmap
 * 30 sigign      bitmap of ignored signals 31 uid         user id
 * 32 ruid        real user id              33 gid         group id
 * 34 rgid        real group id             35 exit_signal sent to parent thread
 * 36 task_cpu    CPU task is scheduled on  37 rt_priority realtime priority
 * 38 policy      man sched_setscheduler    39 blkio_ticks spent wait block IO
 * 40 gtime       guest jiffies of task     41 cgtime      guest jiff of child
 * 42 start_data  program data+bss address  43 end_data    program data+bss
 * 44 start_brk   heap expand with brk()    45 argv0len    argv[0] length
 * 46 uptime      sysinfo.uptime @read time 47 vsz         Virtual Size
 * 48 rss         Resident Set Size         49 shr         Shared memory
 */

// Data layout in toybuf
struct carveup {
  long long slot[50];       // data from /proc, skippint #2 and #3
  unsigned short offset[5]; // offset of fields in str[] (skip name, always 0)
  char state;
  char str[];               // name, tty, command, wchan, attr, cmdline
};

// TODO: Android uses -30 for LABEL, but ideally it would auto-size.
struct typography {
  char *name;
  signed char width, slot;

} static const typos[] = TAGGED_ARRAY(PS,
  // stat#s: PID PPID PRI NI ADDR SZ RSS PGID VSZ MAJFL MINFL PR PSR RTPRIO
  // SCHED
  {"PID", 5, 0}, {"PPID", 5, 1}, {"PRI", 3, 15}, {"NI", 3, 16},
  {"ADDR", 4+sizeof(long), 27}, {"SZ", 5, 20}, {"RSS", 5, 21}, {"PGID", 5, 2},
  {"VSZ", 6, 20}, {"MAJFL", 6, 9}, {"MINFL", 6, 7}, {"PR", 2, 15},
  {"PSR", 3, 36}, {"RTPRIO", 6, 37}, {"SCH", 3, 38},

  // user/group: UID USER RUID RUSER GID GROUP RGID RGROUP
  {"UID", 5, 31}, {"USER", -8, 64|31}, {"RUID", 4, 32}, {"RUSER", -8, 64|32},
  {"GID", 8, 33}, {"GROUP", -8, 64|33}, {"RGID", 4, 34}, {"RGROUP", -8, 64|34},

  // CMD TTY WCHAN LABEL CMDLINE COMMAND
  {"COMM", -15, -1}, {"TTY", -8, -2}, {"WCHAN", -6, -3}, {"LABEL", -30, -4},
  {"COMMAND", -27, -5}, {"CMDLINE", -27, -6}, {"ARGS", -27, -6},
  {"NAME", -15, -6}, {"CMD", -27, -1},

  // TIME ELAPSED TIME+
  {"TIME", 8, 11}, {"ELAPSED", 11, 19}, {"TIME+", 9, 11},

  // Remaining ungrouped
  {"STIME", 5, 19}, {"F", 1, 64|6}, {"S", -1, 64}, {"C", 1, 0}, {"%CPU", 4, 64},
  {"STAT", -5, 64}, {"%VSZ", 5, 23}, {"VIRT", 4, 47}, {"RES", 4, 48},
  {"SHR", 4, 49}
);

// Return 1 to keep, 0 to discard
static int match_process(long long *slot)
{
  struct ptr_len match[] = {
    {&TT.gg, 33}, {&TT.GG, 34}, {&TT.pp, 0}, {&TT.PP, 1}, {&TT.ss, 3},
    {&TT.tt, 4}, {&TT.uu, 31}, {&TT.UU, 32}
  };
  int i, j;
  long *ll = 0;

  // Do we have -g -G -p -P -s -t -u -U options selecting processes?
  for (i = 0; i < ARRAY_LEN(match); i++) {
    struct ptr_len *mm = match[i].ptr;
    if (mm->len) {
      ll = mm->ptr;
      for (j = 0; j<mm->len; j++) if (ll[j] == slot[match[i].len]) return 1;
    }
  }

  // If we had selections and didn't match them, don't display
  if (ll) return 0;

  // Filter implicit categories for other display types
  if ((toys.optflags&(FLAG_a|FLAG_d)) && slot[3]==*slot) return 0;
  if ((toys.optflags&FLAG_a) && !slot[4]) return 0;
  if (!(toys.optflags&(FLAG_a|FLAG_d|FLAG_A|FLAG_e)) && TT.tty!=slot[4])
    return 0;

  return 1;
}

// Convert field to string representation
static char *string_field(struct carveup *tb, struct strawberry *field)
{
  char *buf = toybuf+sizeof(toybuf)-260, *out = buf, *s;
  int which = field->which, sl = typos[which].slot;
  long long *slot = tb->slot, ll = (sl >= 0) ? slot[sl&63] : 0;

  // Default: unsupported (5 "C")
  sprintf(out, "-");

  // stat#s: PID PPID PRI NI ADDR SZ RSS PGID VSZ MAJFL MINFL PR PSR RTPRIO SCH
  if (which <= PS_SCH) {
    char *fmt = "%lld";

    if (which==PS_PRI) ll = 39-ll;
    if (which==PS_ADDR) fmt = "%llx";
    else if (which==PS_SZ) ll >>= 12;
    else if (which==PS_RSS) ll <<= 2;
    else if (which==PS_VSZ) ll >>= 10;
    else if (which==PS_PR && ll<-9) fmt="RT";
    else if (which==PS_RTPRIO && ll == 0) fmt="-";
    sprintf(out, fmt, ll);

  // user/group: UID USER RUID RUSER GID GROUP RGID RGROUP
  } else if (which <= PS_RGROUP) {
    sprintf(out, "%lld", ll);
    if (!(toys.optflags&FLAG_n) && (sl&64)) {
      if (which > PS_RUSER) {
        struct group *gr = getgrgid(ll);

        if (gr) out = gr->gr_name;
      } else {
        struct passwd *pw = getpwuid(ll);

        if (pw) out = pw->pw_name;
      }
    }

  // COMM TTY WCHAN LABEL COMMAND CMDLINE ARGS NAME CMD

  // CMD TTY WCHAN LABEL CMDLINE COMMAND COMM NAME
  } else if (sl < 0) {
    if (which==PS_CMD && (toys.optflags&FLAG_f)) sl = typos[which=PS_ARGS].slot;
    if (slot[45])
      tb->str[tb->offset[4]+slot[45]] = (which == PS_NAME) ? 0 : ' ';
    out = tb->str;
    sl *= -1;
    if (--sl) out += tb->offset[--sl];
    if (which==PS_ARGS)
      for (s = out; *s && *s != ' '; s++) if (*s == '/') out = s+1;
    if (which>=PS_COMMAND && !*out) sprintf(out = buf, "[%s]", tb->str);
  // TIME ELAPSED TIME+
  } else if (which <= PS_TIME_) {
    int unit = 60, pad = 2, j = TT.ticks; 
    time_t seconds;

    if (which!=PS_TIME_) unit *= 60*24;
    else pad = 0;
    if (which==PS_ELAPSED) ll = (slot[46]*j)-slot[19];
    seconds = ll/j;

    // Output days-hours:mins:secs, skipping non-required fields with zero
    // TIME has 3 required fields, ETIME has 2. (Posix!) TIME+ is from top
    for (s = 0, j = 2*(which==PS_TIME_); j<4; j++) {
      if (!s && (seconds>unit || j == 1+(which!=PS_TIME))) s = out;
      if (s) {
        s += sprintf(s, j ? "%0*ld": "%*ld", pad, (long)(seconds/unit));
        pad = 2;
        if ((*s = "-::"[j])) s++;
      }
      seconds %= unit;
      unit /= j ? 60 : 24;
    }
    if (which==PS_TIME_ && s-out<8)
      sprintf(s, ".%02lld", (100*(ll%TT.ticks))/TT.ticks);

  // Posix doesn't specify what flags should say. Man page says
  // 1 for PF_FORKNOEXEC and 4 for PF_SUPERPRIV from linux/sched.h
  } else if (which==PS_F) sprintf(out, "%llo", (slot[6]>>6)&5);
  else if (which==PS_S || which==PS_STAT) {
    s = out;
    *s++ = tb->state;
    if (which==PS_STAT) {
      // TODO l = multithreaded
      if (slot[16]<0) *s++ = '<';
      else if (slot[16]>0) *s++ = 'N';
      if (slot[3]==*slot) *s++ = 's';
      if (slot[18]) *s++ = 'L';
      if (slot[5]==*slot) *s++ = '+';
    } 
    *s = 0;
  } else if (which==PS_STIME) {
    time_t t = time(0)-slot[46]+slot[19]/TT.ticks;

    // Padding behavior's a bit odd: default field size is just hh:mm.
    // Increasing stime:size reveals more data at left until full,
    // so move start address so yyyy-mm-dd hh:mm revealed on left at :16,
    // then add :ss on right for :19.
    strftime(out, 260, "%F %T", localtime(&t));
    out = out+strlen(out)-3-abs(field->len);
    if (out<buf) out = buf;
  } else if (which==PS__CPU || which==PS__VSZ) {
    if (which==PS__CPU) {
      ll = (slot[46]*TT.ticks-slot[19]);
      sl = (slot[11]*1000)/ll;
    } else sl = (slot[23]*1000)/TT.si.totalram;
    sprintf(out, "%d.%d", sl/10, sl%10);
  } else if (which==PS_VIRT || which==PS_RES || which==PS_SHR)
    human_readable(out, slot[typos[which].slot]*sysconf(_SC_PAGESIZE), 0);

  return out;
}

// Display process data that get_ps() read from /proc, formatting with TT.fields
static void show_ps(struct carveup *tb)
{
  struct strawberry *field;
  int i, len, width = TT.width;

  // Loop through fields to display
  for (field = TT.fields; field; field = field->next) {
    char *out = string_field(tb, field);

    // Output the field, appropriately padded
    len = width - (field != TT.fields);
    if (!field->next && field->len<0) i = 0;
    else {
      i = len<abs(field->len) ? len : field->len;
      len = abs(i);
    }

    // TODO test utf8 fontmetrics
    width -= printf(" %*.*s" + (field == TT.fields), i, len, out);
    if (!width) break;
  }
  xputc('\n');
}

// dirtree callback: read data about process to display, store, or discard it.
// Fills toybuf with struct carveup and either DIRTREE_SAVEs a copy to ->extra
// (in -k mode) or calls show_ps on toybuf (no malloc/copy/free there).
static int get_ps(struct dirtree *new)
{
  struct {
    char *name;
    long long bits;
  } fetch[] = {
    {"fd/", _PS_TTY}, {"wchan", _PS_WCHAN}, {"attr/current", _PS_LABEL},
    {"exe", _PS_COMMAND}, {"cmdline",
     (_PS_CMD*!!(toys.optflags&FLAG_f))|_PS_CMDLINE|_PS_ARGS|_PS_NAME}
  };
  struct carveup *tb = (void *)toybuf;
  long long *slot = tb->slot;
  char *name, *s, *buf = tb->str, *end = 0;
  int i, j, fd, ksave = DIRTREE_SAVE*!!(toys.optflags&FLAG_k);
  off_t len;

  // Recurse one level into /proc children, skip non-numeric entries
  if (!new->parent) return DIRTREE_RECURSE|DIRTREE_SHUTUP|ksave;
  if (!(*slot = atol(new->name))) return 0;
  fd = dirtree_parentfd(new);

  len = 2048;
  sprintf(buf, "%lld/stat", *slot);
  if (!readfileat(fd, buf, buf, &len)) return 0;

  // parse oddball fields (name and state). Name can have embedded ')' so match
  // _last_ ')' in stat (although VFS limits filenames to 255 bytes max).
  // All remaining fields should be numeric.
  if (!(name = strchr(buf, '('))) return 0;
  for (s = ++name; *s; s++) if (*s == ')') end = s;
  if (!end || end-name>255) return 0;

  // Parse numeric fields (starting at 4th field in slot[1])
  if (1>sscanf(s = end, ") %c%n", &tb->state, &i)) return 0;
  for (j = 1; j<50; j++) if (1>sscanf(s += i, " %lld%n", slot+j, &i)) break;

  // Now we've read the data, move status and name right after slot[] array,
  // and convert low chars to ? while we're at it.
  for (i = 0; i<end-name; i++)
    if ((tb->str[i] = name[i]) < ' ') tb->str[i] = '?';
  buf = tb->str+i;
  *buf++ = 0;
  len = sizeof(toybuf)-(buf-toybuf);

  // save uid, ruid, gid, gid, and rgid int slots 31-34 (we don't use sigcatch
  // or numeric wchan, and the remaining two are always zero), and vmlck into
  // 18 (which is "obsolete, always 0" from stat)
  slot[31] = new->st.st_uid;
  slot[33] = new->st.st_gid;

  // TIME and TIME+ use combined value, ksort needs 'em added.
  slot[11] += slot[12];

  // If RGROUP RUSER STAT RUID RGID happening, or -G or -U, parse "status"
  // and save ruid, rgid, and vmlck.
  if ((TT.bits&(_PS_RGROUP|_PS_RUSER|_PS_STAT|_PS_RUID|_PS_RGID))
    || TT.GG.len || TT.UU.len)
  {
    off_t temp = len;

    sprintf(buf, "%lld/status", *slot);
    if (!readfileat(fd, buf, buf, &temp)) *buf = 0;
    s = strstr(buf, "\nUid:");
    slot[32] = s ? atol(s+5) : new->st.st_uid;
    s = strstr(buf, "\nGid:");
    slot[34] = s ? atol(s+5) : new->st.st_gid;
    s = strstr(buf, "\nVmLck:");
    if (s) slot[18] = atoll(s+5);
  }

  // We now know enough to skip processes we don't care about.
  if (!match_process(slot)) return 0;

  // Do we need to read "statm"?
  if (TT.bits&(_PS_VIRT|_PS_RES|_PS_SHR)) {
    off_t temp = len;

    sprintf(buf, "%lld/statm", *slot);
    if (!readfileat(fd, buf, buf, &temp)) *buf = 0;
    
    for (s = buf, i=0; i<3; i++)
      if (!sscanf(s, " %lld%n", slot+47+i, &j)) slot[47+i] = 0;
      else s += j;
  }

  // /proc data is generated as it's read, so for maximum accuracy on slow
  // systems (or ps | more) we re-fetch uptime as we fetch each /proc line.
  sysinfo(&TT.si);
  slot[46] = TT.si.uptime;

  // Fetch string data while parentfd still available, appending to buf.
  // (There's well over 3k of toybuf left. We could dynamically malloc, but
  // it'd almost never get used, querying length of a proc file is awkward,
  // fixed buffer is nommu friendly... Wait for somebody to complain. :)
  slot[45] = 0;
  for (j = 0; j<ARRAY_LEN(fetch); j++) { 
    tb->offset[j] = buf-(tb->str);
    if (!(TT.bits&fetch[j].bits)) {
      *buf++ = 0;
      continue;
    }

    // Determine remaining space, reserving minimum of 256 bytes/field and
    // 260 bytes scratch space at the end (for output conversion later).
    len = sizeof(toybuf)-(buf-toybuf)-260-256*(ARRAY_LEN(fetch)-j);
    sprintf(buf, "%lld/%s", *slot, fetch[j].name);

    // For cmdline we readlink instead of read contents
    if (j==3) {
      if (0>=(len = readlinkat(fd, buf, buf, len))) buf[len] = 0;
 
    // If it's not the TTY field, data we want is in a file.
    // Last length saved in slot[] is command line (which has embedded NULs)
    } else if (!j) {
      int rdev = slot[4];
      struct stat st;

      // Call no tty "?" rather than "0:0".
      strcpy(buf, "?");
      if (rdev) {
        // Can we readlink() our way to a name?
        for (i = 0; i<3; i++) {
          sprintf(buf, "%lld/fd/%i", *slot, i);
          if (!fstatat(fd, buf, &st, 0) && S_ISCHR(st.st_mode)
            && st.st_rdev == rdev && 0<(len = readlinkat(fd, buf, buf, len)))
          {
            buf[len] = 0;
            break;
          }
        }

        // Couldn't find it, try all the tty drivers.
        if (i == 3) {
          FILE *fp = fopen("/proc/tty/drivers", "r");
          int tty_major = 0, maj = major(rdev), min = minor(rdev);

          if (fp) {
            while (fscanf(fp, "%*s %256s %d %*s %*s", buf, &tty_major) == 2) {
              // TODO: we could parse the minor range too.
              if (tty_major == maj) {
                sprintf(buf+strlen(buf), "%d", min);
                if (!stat(buf, &st) && S_ISCHR(st.st_mode) && st.st_rdev==rdev)
                  break;
              }
              tty_major = 0;
            }
            fclose(fp);
          }

          // Really couldn't find it, so just show major:minor.
          if (!tty_major) sprintf(buf, "%d:%d", maj, min);
        }

        s = buf;
        if (strstart(&s, "/dev/")) memmove(buf, s, strlen(s)+1);
      }

    // Data we want is in a file.
    // Last length saved in slot[] is command line (which has embedded NULs)
    } else {
      readfileat(fd, buf, buf, &len);

      // When command has no arguments, don't space over the NUL
      if (len>0) {
        int temp = 0;

        if (buf[len-1]=='\n') buf[--len] = 0;

        // Turn NUL to space, other low ascii to ?
        for (i=0; i<len; i++) {
          char c = buf[i];

          if (!c) {
            if (!temp) temp = i;
            c = ' ';
          } else if (c<' ') c = '?';
          buf[i] = c;
        }
        len = temp; // position of _first_ NUL
      } else *buf = len = 0;
      // Store end of argv[0] so NAME and CMDLINE can differ.
      slot[45] = len;
    }

    buf += strlen(buf)+1;
  }

  // If we need to sort the output, add it to the list and return.
  if (ksave) {
    s = xmalloc(buf-toybuf);
    new->extra = (long)s;
    memcpy(s, toybuf, buf-toybuf);
    TT.kcount++;

  // Otherwise display it now
  } else show_ps(tb);

  return ksave;
}

// Traverse arg_list of csv, calling callback on each value
void comma_args(struct arg_list *al, void *data, char *err,
  char *(*callback)(void *data, char *str, int len))
{
  char *next, *arg;
  int len;

  while (al) {
    arg = al->arg;
    while ((next = comma_iterate(&arg, &len)))
      if ((next = callback(data, next, len)))
        perror_exit("%s '%s'\n%*c", err, al->arg,
          (int)(5+strlen(toys.which->name)+strlen(err)+next-al->arg), '^');
    al = al->next;
  }
}

static char *parse_ko(void *data, char *type, int length)
{
  struct strawberry *field;
  char *width, *title, *end, *s;
  int i, j, k;

  // Get title, length of title, type, end of type, and display width

  // Chip off =name to display
  if ((end = strchr(type, '=')) && length>(end-type)) {
    title = end+1;
    length -= (end-type)+1;
  } else {
    end = type+length;
    title = 0;
  }

  // Chip off :width to display
  if ((width = strchr(type, ':')) && width<end) {
    if (!title) length = width-type;
  } else width = 0;

  // Allocate structure, copy title
  field = xzalloc(sizeof(struct strawberry)+(length+1)*!!title);
  if (title) {
    memcpy(field->title = field->forever, title, length);
    field->title[field->len = length] = 0;
  }

  if (width) {
    field->len = strtol(++width, &title, 10);
    if (!isdigit(*width) || title != end) return title;
    end = --width;
  }

  // Find type
  if (*(struct strawberry **)data == TT.kfields) {
    field->reverse = 1;
    if (*type == '-') field->reverse = -1;
    else if (*type != '+') type--;
    type++;
  }
  for (i = 0; i<ARRAY_LEN(typos); i++) {
    field->which = i;
    for (j = 0; j<2; j++) {
      if (!j) s = typos[i].name;
      // posix requires alternate names for some fields
      else if (-1==(k = stridx((char []){PS_NI, PS_SCH, PS_ELAPSED, PS__CPU,
        PS_VSZ, PS_USER, 0}, i))) continue;
      else {
        s = ((char *[]){"NICE", "SCHED", "ETIME", "PCPU", "VSIZE", "UNAME"})[k];
      }

      if (!strncasecmp(type, s, end-type) && strlen(s)==end-type) break;
    }
    if (j!=2) break;
  }
  if (i==ARRAY_LEN(typos)) return type;
  if (!field->title) field->title = typos[field->which].name;
  if (!field->len) field->len = typos[field->which].width;
  else if (typos[field->which].width<0) field->len *= -1;
  dlist_add_nomalloc(data, (void *)field);

  // Print padded header for -o.
  if (*(struct strawberry **)data == TT.fields) {
    TT.header_len +=
      snprintf(toybuf + TT.header_len, sizeof(toybuf) - TT.header_len,
               " %*s" + (field == TT.fields), field->len, field->title);
    TT.bits |= 1LL<<field->which;
  }

  return 0;
}

// Parse -p -s -t -u -U -g -G
static char *parse_rest(void *data, char *str, int len)
{
  struct ptr_len *pl = (struct ptr_len *)data;
  long *ll = pl->ptr;
  char *end;
  int num = 0;

  // numeric: -p, -s
  // gg, GG, pp, ss, tt, uu, UU, *parsing;
 
  // Allocate next chunk of data
  if (!(15&pl->len))
    ll = pl->ptr = xrealloc(pl->ptr, sizeof(long)*(pl->len+16));

  // Parse numerical input
  if (isdigit(*str)) {
    ll[pl->len] = xstrtol(str, &end, 10);
    if (end==(len+str)) num++;
  }

  if (pl==&TT.pp || pl==&TT.ss) {
    if (num && ll[pl->len]>0) {
      pl->len++;

      return 0;
    }
  } else if (pl==&TT.tt) {
    // -t pts = 12,pts/12 tty = /dev/tty2,tty2,S0
    if (!num) {
      if (strstart(&str, strcpy(toybuf, "/dev/"))) len -= 5;
      if (strstart(&str, "pts/")) {
        len -= 4;
        num++;
      } else if (strstart(&str, "tty")) len -= 3;
    }
    if (len<256 && (!(end = strchr(str, '/')) || end-str>len)) {
      struct stat st;

      end = toybuf + sprintf(toybuf, "/dev/%s", num ? "pts/" : "tty");
      memcpy(end, str, len);
      end[len] = 0;
      xstat(toybuf, &st);
      ll[pl->len++] = st.st_rdev;

      return 0;
    }
  } else if (len<255) {
    char name[256];

    if (num) {
      pl->len++;

      return 0;
    }

    memcpy(name, str, len);
    name[len] = 0;
    if (pl==&TT.gg || pl==&TT.GG) {
      struct group *gr = getgrnam(name);
      if (gr) {
        ll[pl->len++] = gr->gr_gid;

        return 0;
      }
    } else if (pl==&TT.uu || pl==&TT.UU) {
      struct passwd *pw = getpwnam(name);
      if (pw) {
        ll[pl->len++] = pw->pw_uid;

        return 0;
      }
    }
  }

  // Return error
  return str;
}

// sort for -k
static int ksort(void *aa, void *bb)
{
  struct strawberry *field;
  struct carveup *ta = *(struct carveup **)aa, *tb = *(struct carveup **)bb;
  int ret = 0, slot;

  for (field = TT.kfields; field; field = field->next) {

    slot = typos[field->which].slot;
    // Compare as strings?
    if (slot&64) {
      memccpy(toybuf, string_field(ta, field), 0, 2048);
      toybuf[2048] = 0;
      ret = strcmp(toybuf, string_field(tb, field));
    } else {
      if (ta->slot[slot]<tb->slot[slot]) ret = -1;
      if (ta->slot[slot]>tb->slot[slot]) ret = 1;
    }

    if (ret) return ret*field->reverse;
  }

  return 0;
}

void ps_main(void)
{
  struct dirtree *dt;
  int i;

  TT.ticks = sysconf(_SC_CLK_TCK);
  TT.width = 99999;
  if (!(toys.optflags&FLAG_w)) terminal_size(&TT.width, 0);

  // find controlling tty, falling back to /dev/tty if none
  for (i = 0; !TT.tty && i<4; i++) {
    struct stat st;
    int fd = i;

    if (i==3 && -1==(fd = open("/dev/tty", O_RDONLY))) break;

    if (isatty(fd) && !fstat(fd, &st)) TT.tty = st.st_rdev;
    if (i==3) close(fd);
  }

  // parse command line options other than -o
  comma_args(TT.ps.P, &TT.PP, "bad -P", parse_rest);
  comma_args(TT.ps.p, &TT.pp, "bad -p", parse_rest);
  comma_args(TT.ps.t, &TT.tt, "bad -t", parse_rest);
  comma_args(TT.ps.s, &TT.ss, "bad -s", parse_rest);
  comma_args(TT.ps.u, &TT.uu, "bad -u", parse_rest);
  comma_args(TT.ps.U, &TT.UU, "bad -u", parse_rest);
  comma_args(TT.ps.g, &TT.gg, "bad -g", parse_rest);
  comma_args(TT.ps.G, &TT.GG, "bad -G", parse_rest);
  comma_args(TT.ps.k, &TT.kfields, "bad -k", parse_ko);
  dlist_terminate(TT.kfields);

  // Parse manual field selection, or default/-f/-l, plus -Z,
  // constructing the header line in toybuf as we go.
  if (toys.optflags&FLAG_Z) {
    struct arg_list Z = { 0, "LABEL" };

    comma_args(&Z, &TT.fields, "-Z", parse_ko);
  }
  if (TT.ps.o) comma_args(TT.ps.o, &TT.fields, "bad -o field", parse_ko);
  else {
    struct arg_list al;

    al.next = 0;
    if (toys.optflags&FLAG_f)
      al.arg = "USER:8=UID,PID,PPID,C,STIME,TTY,TIME,CMD";
    else if (toys.optflags&FLAG_l)
      al.arg = "F,S,UID,PID,PPID,C,PRI,NI,ADDR,SZ,WCHAN,TTY,TIME,CMD";
    else if (CFG_TOYBOX_ON_ANDROID)
      al.arg = "USER,PID,PPID,VSIZE,RSS,WCHAN:10,ADDR:10=PC,S,CMDLINE";
    else al.arg = "PID,TTY,TIME,CMD";

    comma_args(&al, &TT.fields, 0, parse_ko);
  }
  dlist_terminate(TT.fields);
  printf("%s\n", toybuf);

  dt = dirtree_read("/proc", get_ps);

  if (toys.optflags&FLAG_k) {
    struct carveup **tbsort = xmalloc(TT.kcount*sizeof(struct carveup *));

    // descend into child list
    *tbsort = (void *)dt;
    dt = dt->child;
    free(*tbsort);

    // populate array
    i = 0;
    while (dt) {
      void *temp = dt->next;

      tbsort[i++] = (void *)dt->extra;
      free(dt);
      dt = temp;
    }

    // Sort and show
    qsort(tbsort, TT.kcount, sizeof(struct carveup *), (void *)ksort);
    for (i = 0; i<TT.kcount; i++) {
      show_ps(tbsort[i]);
      free(tbsort[i]);
    }
    if (CFG_TOYBOX_FREE) free(tbsort);
  }

  if (CFG_TOYBOX_FREE) {
    free(TT.gg.ptr);
    free(TT.GG.ptr);
    free(TT.pp.ptr);
    free(TT.PP.ptr);
    free(TT.ss.ptr);
    free(TT.tt.ptr);
    free(TT.uu.ptr);
    free(TT.UU.ptr);
    llist_traverse(TT.fields, free);
  }
}

#define CLEANUP_ps
#define FOR_top
#include "generated/flags.h"

void ttop_main(void)
{
  ps_main();
}
