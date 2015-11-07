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
 * TODO: ps aux (att & bsd style "ps -ax" vs "ps ax" behavior difference)
 * TODO: finalize F, remove C
 *       switch -fl to -y, use "string" instead of constants to set, remove C
 * TODO: --sort
 * TODO: way too many hardwired constants here, how can I generate them?
 * TODO: thread support /proc/$d/task/%d/stat (and -o stat has "l")
 *
 * Design issue: the -o fields are an ordered array, and the order is
 * significant. The array index is used in strawberry->which (consumed
 * in do_ps()) and in the bitmasks enabling default fields in ps_main().

USE_PS(NEWTOY(ps, "P(ppid)*aAdeflno*p(pid)*s*t*u*U*g*G*wZ[!ol][+Ae]", TOYFLAG_USR|TOYFLAG_BIN))

config PS
  bool "ps"
  default y
  help
    usage: ps [-AadeflnwZ] [-gG GROUP] [-o FIELD] [-p PID] [-t TTY] [-uU USER]

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

    -n	Show numeric USER and GROUP
    -w	Wide output (don't truncate at terminal width)

    Which FIELDs to show. (Default = -o PID,TTY,TIME,CMD)

    -f	Full listing (-o USER:8=UID,PID,PPID,C,STIME,TTY,TIME,CMD)
    -l	Long listing (-o F,S,UID,PID,PPID,C,PRI,NI,ADDR,SZ,WCHAN,TTY,TIME,CMD)
    -o	Output the listed FIELDs, each with optional :size and/or =title
    -Z	Include LABEL

    Available -o FIELDs:

      ADDR    Instruction pointer
      CMD     Command line (from /proc/pid/cmdline, including args)
      CMDLINE Command line (from /proc/pid/cmdline, no args)
      COMM    Command name (from /proc/pid/stat, no args)
      ETIME   Elapsed time since process start
      F       Process flags (PF_*) from linux source file include/sched.h
              (in octal rather than hex because posix)
      GID     Group id
      GROUP   Group name
      LABEL   Security label
      MAJFL   Major page faults
      MINFL   Minor page faults
      NI      Niceness of process (lower niceness is higher priority)
      PCPU    Percentage of CPU time used
      PGID    Process Group ID
      PID     Process ID
      PPID    Parent Process ID
      PRI     Priority
      RGID    Real (before sgid) group ID
      RGROUP  Real (before sgid) group name
      RSS     Resident Set Size (memory currently used)
      RUID    Real (before suid) user ID
      RUSER   Real (before suid) user name
      S       Process state:
              R (running) S (sleeping) D (disk sleep) T (stopped)  t (traced)
              Z (zombie)  X (dead)     x (dead)       K (wakekill) W (waking)
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
*/

#define FOR_ps
#include "toys.h"

GLOBALS(
  struct arg_list *G;
  struct arg_list *g;
  struct arg_list *U;
  struct arg_list *u;
  struct arg_list *t;
  struct arg_list *s;
  struct arg_list *p;
  struct arg_list *o;
  struct arg_list *P;

  struct ptr_len gg, GG, pp, PP, ss, tt, uu, UU, *parsing;
  unsigned width;
  dev_t tty;
  void *fields;
  long bits;
  long long ticks;
  size_t header_len;
)

struct strawberry {
  struct strawberry *next, *prev;
  short which, len;
  char *title;
  char forever[];
};

static time_t get_uptime(void)
{
  struct sysinfo si;

  sysinfo(&si);

  return si.uptime;
}

// Return 1 to display, 0 to skip
static int match_process(long long *slot)
{
  struct ptr_len *match[] = {
    &TT.gg, &TT.GG, &TT.pp, &TT.PP, &TT.ss, &TT.tt, &TT.uu, &TT.UU
  };
  int i, j, mslot[] = {33, 34, 0, 1, 3, 4, 31, 32};
  long *ll = 0;

  // Do we have -g -G -p -P -s -t -u -U options selecting processes?
  for (i = 0; i < ARRAY_LEN(match); i++) {
    if (match[i]->len) {
      ll = match[i]->ptr;
      for (j = 0; j<match[i]->len; j++) if (ll[j] == slot[mslot[i]]) return 1;
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

static void find_tty_name(char *out, int rdev) {
  int major = (rdev>>8)&0xfff, minor = ((rdev>>12)&0xfff00)|(rdev&0xff);
  FILE *fp = fopen("/proc/tty/drivers", "r");

  if (fp) {
    int tty_major;

    while (fscanf(fp, "%*s %s %d %*s %*s", out, &tty_major) == 2) {
      // TODO: we could parse the minor range too.
      if (tty_major == major) {
        struct stat st;

        sprintf(out + strlen(out), "%d", minor);
        if (!stat(out, &st) && S_ISCHR(st.st_mode) && st.st_rdev == rdev) {
          fclose(fp);
          return;
        }
      }
    }
    fclose(fp);
  }

  // Really couldn't find it, so just show major:minor.
  sprintf(out, "%d:%d", major, minor);
}

// dirtree callback.
// toybuf used as: 1024 /proc/$PID/stat, 1024 slot[], 2048 /proc/$PID/cmdline
static int do_ps(struct dirtree *new)
{
  struct strawberry *field;
  long long *slot = (void *)(toybuf+1024), ll;
  char *name, *s, state;
  int nlen, i, fd, len, width = TT.width;

  if (!new->parent) return DIRTREE_RECURSE|DIRTREE_SHUTUP;
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

  // parse numeric fields (PID = 0, skip 2, then 4th field goes in slot[1])
  for (len = 1; len<100; len++)
    if (1>sscanf(s += i, " %lld%n", slot+len, &i)) break;

  // save uid, ruid, gid, gid, and rgid int slots 31-34 (we don't use sigcatch
  // or numeric wchan, and the remaining two are always zero).
  slot[31] = new->st.st_uid;
  slot[33] = new->st.st_gid;

  // If RGROUP RUSER STAT RUID RGID
  // Save ruid in slot[34] and rgid in slot[35], which are otherwise zero,
  // and vmlck into slot[18] (it_real_value, also always zero).
  if ((TT.bits & 0x38300000) || TT.GG.len || TT.UU.len) {
    char *out = toybuf+2048;

    sprintf(out, "%lld/status", *slot);
    if (!readfileat(dirtree_parentfd(new), out, out, 2048)) *out = 0;
    s = strstr(out, "\nUid:");
    slot[32] = s ? atol(s+5) : new->st.st_uid;
    s = strstr(out, "\nGid:");
    slot[34] = s ? atol(s+5) : new->st.st_gid;
    s = strstr(out, "\nVmLck:");
    if (s) slot[18] = atoll(s+5);
  }

  // skip processes we don't care about.
  if (!match_process(slot)) return 0;

  // At this point 512 bytes at toybuf+512 are free (already parsed).
  // Start of toybuf still has name in it.

  // Loop through fields
  for (field = TT.fields; field; field = field->next) {
    char *out = toybuf+2048, *scratch = toybuf+512;

    // Default: unsupported (5 "C")
    sprintf(out, "-");

    // PID, PPID, PRI, NI, ADDR, SZ, RSS, PGID, VSS, MAJFL, MINFL
    if (-1!=(i = stridx((char[]){3,4,6,7,8,9,24,19,23,25,30,0}, field->which)))
    {
      char *fmt = "%lld";

      ll = slot[((char[]){0,1,15,16,27,20,21,2,20,9,7})[i]];
      if (i==2) ll--;
      if (i==4) fmt = "%llx";
      else if (i==5) ll >>= 12;
      else if (i==6) ll <<= 2;
      else if (i==8) ll >>= 10;
      sprintf(out, fmt, ll);
    // UID USER RUID RUSER GID GROUP RGID RGROUP
    } else if (-1!=(i = stridx((char[]){2,22,28,21,26,17,29,20}, field->which)))
    {
      int id = slot[31+i/2]; // uid, ruid, gid, rgid

      // Even entries are numbers, odd are names
      sprintf(out, "%d", id);
      if (!(toys.optflags&FLAG_n) && i&1) {
        if (i>3) {
          struct group *gr = getgrgid(id);

          if (gr) out = gr->gr_name;
        } else {
          struct passwd *pw = getpwuid(id);

          if (pw) out = pw->pw_name;
        }
      }
 
    // F (also assignment of i used by later tests)
    // Posix doesn't specify what flags should say. Man page says
    // 1 for PF_FORKNOEXEC and 4 for PF_SUPERPRIV from linux/sched.h
    } else if (!(i = field->which)) sprintf(out, "%llo", (slot[6]>>6)&5);
    // S STAT
    else if (i==1 || i==27) {
      sprintf(out, "%c", state);
      if (i==27) {
        // TODO l = multithreaded
        s = out+1;
        if (slot[16]<0) *s++ = '<';
        else if (slot[16]>0) *s++ = 'N';
        if (slot[3]==*slot) *s++ = 's';
        if (slot[18]) *s++ = 'L';
        if (slot[5]==*slot) *s++ = '+';
        *s = 0;
      } 
    // WCHAN
    } else if (i==10) {
      sprintf(scratch, "%lld/wchan", *slot);
      readfileat(dirtree_parentfd(new), scratch, out, 2047);

    // LABEL
    } else if (i==31) {
      sprintf(scratch, "%lld/attr/current", *slot);
      readfileat(dirtree_parentfd(new), scratch, out, 2047);
      chomp(out);

    // STIME
    } else if (i==11) {
      time_t t = time(0) - get_uptime() + slot[19]/sysconf(_SC_CLK_TCK);

      // Padding behavior's a bit odd: default field size is just hh:mm.
      // Increasing stime:size reveals more data at left until full
      // yyyy-mm-dd hh:mm revealed at :16, then adds :ss at end for :19. But
      // expanding last field just adds :ss.
      strftime(scratch, 512, "%F %T", localtime(&t));
      out = scratch+strlen(scratch)-3-abs(field->len);
      if (out<scratch) out = scratch;

    // TTY
    } else if (i==12) {
      int rdev = slot[4];

      // The common case is no tty, and we call that "?" rather than "0:0".
      if (rdev == 0) strcpy(out, "?");
      else {
        // Can we readlink() our way to a name?
        for (i=0; i<3; i++) {
          struct stat st;

          sprintf(scratch, "%lld/fd/%i", *slot, i);
          fd = dirtree_parentfd(new);
          if (!fstatat(fd, scratch, &st, 0) && S_ISCHR(st.st_mode)
            && st.st_rdev == rdev
            && 0<(len = readlinkat(fd, scratch, out, 2047)))
          {
            out[len] = 0;
            break;
          }
        }

        // Couldn't find it, try all the tty drivers.
        if (i == 3) find_tty_name(out, rdev);

        if (!strncmp(out, "/dev/", 5)) out += 5;
      }

    // TIME ELAPSED
    } else if (i==13 || i==16) {
      int unit = 60*60*24, j = sysconf(_SC_CLK_TCK);
      time_t seconds = (i==16) ? (get_uptime()*j)-slot[19] : slot[11]+slot[12];

      seconds /= j;
      for (s = 0, j = 0; j<4; j++) {
        // TIME has 3 required fields, ETIME has 2. (Posix!)
        if (!s && (seconds>unit || j == 1+(i==16))) s = out;
        if (s) {
          s += sprintf(s, j ? "%02ld": "%2ld", (long)(seconds/unit));
          if ((*s = "-::"[j])) s++;
        }
        seconds %= unit;
        unit /= j ? 60 : 24;
      }

    // COMM - command line including arguments
    // Command line limited to 2k displayable. We could dynamically malloc, but
    // it'd almost never get used, querying length of a proc file is awkward,
    // fixed buffer is nommu friendly... Wait for somebody to complain. :)
    // CMDLINE - command line from /proc/pid/cmdline without arguments
    } else if (i==14 || i==32) {
      int fd;

      len = 0;
      sprintf(out, "%lld/cmdline", *slot);
      fd = openat(dirtree_parentfd(new), out, O_RDONLY);
 
      if (fd != -1) {
        if (0<(len = read(fd, out, 2047))) {
          if (!out[len-1]) len--;
          else out[len] = 0;
          if (i==14) for (i = 0; i<len; i++) if (out[i] < ' ') out[i] = ' ';
        }
        close(fd);
      }

      if (len<1) sprintf(out, "[%.*s]", nlen, name);

    // CMD - command name (without arguments)
    } else if (i==15) {
      sprintf(out, "%.*s", nlen, name);

    // %CPU
    } else if (i==18) {
      ll = (get_uptime()*sysconf(_SC_CLK_TCK)-slot[19]);
      len = ((slot[11]+slot[12])*1000)/ll;
      sprintf(out, "%d.%d", len/10, len%10);
    }

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

  return 0;
}

// Traverse arg_list of csv, calling callback on each value
void comma_args(struct arg_list *al, char *err,
  char *(*callback)(char *str, int len))
{
  char *next, *arg;
  int len;

  while (al) {
    arg = al->arg;
    while ((next = comma_iterate(&arg, &len)))
      if ((next = callback(next, len)))
        perror_exit("%s '%s'\n% *c", err, al->arg,
                    strlen(toys.which->name) + 2 +
                    strlen(err) + 2 + 1+next-al->arg, '^');
    al = al->next;
  }
}

static char *parse_o(char *type, int length)
{
  struct strawberry *field;
  char *width, *title, *end, *s, *typos[] = {
         "F", "S", "UID", "PID", "PPID", "C", "PRI", "NI", "ADDR", "SZ",
         "WCHAN", "STIME", "TTY", "TIME", "CMD", "COMMAND", "ELAPSED", "GROUP",
         "%CPU", "PGID", "RGROUP", "RUSER", "USER", "VSZ", "RSS", "MAJFL",
         "GID", "STAT", "RUID", "RGID", "MINFL", "LABEL", "CMDLINE"
  };
  // TODO: Android uses -30 for LABEL, but ideally it would auto-size.
  signed char widths[] = {1,-1,5,5,5,2,3,3,4+sizeof(long),5,
                          -6,5,-8,8,-27,-27,11,-8,
                          4,5,-8,-8,-8,6,5,6,
                          8,-5,4,4,6,-30,-27};
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
  for (i = 0; i<ARRAY_LEN(typos); i++) {
    field->which = i;
    for (j = 0; j<2; j++) {
      if (!j) s = typos[i];
      // posix requires alternate names for some fields
      else if (-1==(k = stridx((char []){7,14,15,16,18,23,22,0}, i))) continue;
      else s = ((char *[]){"NICE","ARGS","COMM","ETIME","PCPU",
                           "VSIZE","UNAME"})[k];

      if (!strncasecmp(type, s, end-type) && strlen(s)==end-type) break;
    }
    if (j!=2) break;
  }
  if (i==ARRAY_LEN(typos)) return type;
  if (!field->title) field->title = typos[field->which];
  if (!field->len) field->len = widths[field->which];
  else if (widths[field->which]<0) field->len *= -1;
  dlist_add_nomalloc((void *)&TT.fields, (void *)field);

  // Print padded header.
  TT.header_len +=
    snprintf(toybuf + TT.header_len, sizeof(toybuf) - TT.header_len,
             " %*s" + (field == TT.fields), field->len, field->title);
  TT.bits |= (i = 1<<field->which);

  return 0;
}

// Parse -p -s -t -u -U -g -G
static char *parse_rest(char *str, int len)
{
  struct ptr_len *pl = TT.parsing;
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

void ps_main(void)
{
  int i;

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
  TT.parsing = &TT.PP;
  comma_args(TT.P, "bad -P", parse_rest);
  TT.parsing = &TT.pp;
  comma_args(TT.p, "bad -p", parse_rest);
  TT.parsing = &TT.tt;
  comma_args(TT.t, "bad -t", parse_rest);
  TT.parsing = &TT.ss;
  comma_args(TT.s, "bad -s", parse_rest);
  TT.parsing = &TT.uu;
  comma_args(TT.u, "bad -u", parse_rest);
  TT.parsing = &TT.UU;
  comma_args(TT.U, "bad -u", parse_rest);
  TT.parsing = &TT.gg;
  comma_args(TT.g, "bad -g", parse_rest);
  TT.parsing = &TT.GG;
  comma_args(TT.G, "bad -G", parse_rest);

  // Parse manual field selection, or default/-f/-l, plus -Z,
  // constructing the header line in toybuf as we go.
  if (toys.optflags&FLAG_Z) {
    struct arg_list Z = { 0, "LABEL" };

    comma_args(&Z, "-Z", parse_o);
  }
  if (TT.o) comma_args(TT.o, "bad -o field", parse_o);
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

    comma_args(&al, 0, parse_o);
  }
  dlist_terminate(TT.fields);
  printf("%s\n", toybuf);

  dirtree_read("/proc", do_ps);

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
