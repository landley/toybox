/* strace.c - Trace system calls.
 *
 * Copyright 2020 The Android Open Source Project
 *
 * See https://man7.org/linux/man-pages/man2/syscall.2.html

USE_STRACE(NEWTOY(strace, "^p#s#v", TOYFLAG_USR|TOYFLAG_SBIN))

config STRACE
  bool "strace"
  default n
  help
    usage: strace [-fv] [-p PID] [-s NUM] COMMAND [ARGS...]

    Trace systems calls made by a process.

    -s	String length limit.
    -v	Dump all of large structs/arrays.
*/

#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>

#define FOR_strace
#include "toys.h"

GLOBALS(
  long s, p;

  char ioctl[32], *fmt;
  long regs[256/sizeof(long)], syscall;
  pid_t pid;
  int arg;
)

  struct user_regs_struct regs;


// Syscall args from https://man7.org/linux/man-pages/man2/syscall.2.html
// REG_ORDER is args 0-6, SYSCALL, RESULT
#if defined(__ARM_EABI__)
static const char REG_ORDER[] = {0,1,2,3,4,5,7,0};
#elif defined(__ARM_ARCH) && __ARM_ARCH == 8
static const char REG_ORDER[] = {0,1,2,3,4,5,8,0};
#elif defined(__i386__)
// ebx,ecx,edx,esi,edi,ebp,orig_eax,eax
static const char REG_ORDER[] = {0,1,2,3,4,5,11,6};
#elif defined(__m68k__)
// d1,d2,d3,d4,d5,a0,orig_d0,d0
static const char REG_ORDER[] = {0,1,2,3,4,7,16,14};
#elif defined(__PPC__) || defined(__PPC64__)
static const char REG_ORDER[] = {3,4,5,6,7,8,0,3};
#elif defined(__s390__) // also covers s390x
// r2,r3,r4,r5,r6,r7,r1,r2 but mask+addr before r0 so +2
static const char REG_ORDER[] = {4,5,6,7,8,9,3,4};
#elif defined(__sh__)
static const char REG_ORDER[] = {4,5,6,7,0,1,3,0};
#elif defined(__x86_64__)
// rdi,rsi,rdx,r10,r8,r9,orig_rax,rax
static const char REG_ORDER[] = {14,13,12,7,9,8,15,10};
#else
#error unsupported architecture
#endif

#define C(x) case x: return #x

#define FS_IOC_FSGETXATTR 0x801c581f
#define FS_IOC_FSSETXATTR 0x401c5820
#define FS_IOC_GETFLAGS 0x80086601
#define FS_IOC_SETFLAGS 0x40086602
#define FS_IOC_GETVERSION 0x80087601
#define FS_IOC_SETVERSION 0x40047602
struct fsxattr {
  unsigned fsx_xflags;
  unsigned fsx_extsize;
  unsigned fsx_nextents;
  unsigned fsx_projid;
  unsigned fsx_cowextsize;
  char fsx_pad[8];
};

static char *strioctl(int i)
{
  switch (i) {
    C(FS_IOC_FSGETXATTR);
    C(FS_IOC_FSSETXATTR);
    C(FS_IOC_GETFLAGS);
    C(FS_IOC_GETVERSION);
    C(FS_IOC_SETFLAGS);
    C(FS_IOC_SETVERSION);
    C(SIOCGIFADDR);
    C(SIOCGIFBRDADDR);
    C(SIOCGIFCONF);
    C(SIOCGIFDSTADDR);
    C(SIOCGIFFLAGS);
    C(SIOCGIFHWADDR);
    C(SIOCGIFMAP);
    C(SIOCGIFMTU);
    C(SIOCGIFNETMASK);
    C(SIOCGIFTXQLEN);
    C(TCGETS);
    C(TCSETS);
    C(TIOCGWINSZ);
    C(TIOCSWINSZ);
  }
  sprintf(toybuf, "%#x", i);
  return toybuf;
}

// TODO: move to lib, implement errno(1)?
static char *strerrno(int e)
{
  switch (e) {
    // uapi errno-base.h
    C(EPERM);
    C(ENOENT);
    C(ESRCH);
    C(EINTR);
    C(EIO);
    C(ENXIO);
    C(E2BIG);
    C(ENOEXEC);
    C(EBADF);
    C(ECHILD);
    C(EAGAIN);
    C(ENOMEM);
    C(EACCES);
    C(EFAULT);
    C(ENOTBLK);
    C(EBUSY);
    C(EEXIST);
    C(EXDEV);
    C(ENODEV);
    C(ENOTDIR);
    C(EISDIR);
    C(EINVAL);
    C(ENFILE);
    C(EMFILE);
    C(ENOTTY);
    C(ETXTBSY);
    C(EFBIG);
    C(ENOSPC);
    C(ESPIPE);
    C(EROFS);
    C(EMLINK);
    C(EPIPE);
    C(EDOM);
    C(ERANGE);
    // uapi errno.h
    C(EDEADLK);
    C(ENAMETOOLONG);
    C(ENOLCK);
    C(ENOSYS);
    C(ENOTEMPTY);
    C(ELOOP);
    C(ENOMSG);
    // ...etc; fill in as we see them in practice?
  }
  sprintf(toybuf, "%d", e);
  return toybuf;
}

#undef C

static void xptrace(int req, pid_t pid, void *addr, void *data)
{
  if (ptrace(req, pid, addr, data)) perror_exit("ptrace pid %d", pid);
}

static void get_regs()
{
  xptrace(PTRACE_GETREGS, TT.pid, 0, TT.regs);
}

static void ptrace_struct(long addr, void *dst, size_t bytes)
{
  int offset = 0, i;
  long v;

  for (i=0; i<bytes; i+=sizeof(long)) {
    errno = 0;
    v = ptrace(PTRACE_PEEKDATA, TT.pid, addr + offset);
    if (errno) perror_exit("PTRACE_PEEKDATA failed");
    memcpy(dst + offset, &v, sizeof(v));
    offset += sizeof(long);
  }
}

// TODO: this all relies on having the libc structs match the kernel structs,
// which isn't always true for glibc...
static void print_struct(long addr)
{
  if (!addr) { // All NULLs look the same...
    fprintf(stderr, "NULL");
    while (*TT.fmt != '}') ++TT.fmt;
    ++TT.fmt;
  } else if (strstart(&TT.fmt, "ifreq}")) {
    struct ifreq ir;

    ptrace_struct(addr, &ir, sizeof(ir));
    // TODO: is this always an ioctl? use TT.regs[REG_ORDER[1]] to work out what to show.
    fprintf(stderr, "{...}");
  } else if (strstart(&TT.fmt, "fsxattr}")) {
    struct fsxattr fx;

    ptrace_struct(addr, &fx, sizeof(fx));
    fprintf(stderr, "{fsx_xflags=%#x, fsx_extsize=%d, fsx_nextents=%d, "
        "fsx_projid=%d, fsx_cowextsize=%d}", fx.fsx_xflags, fx.fsx_extsize,
        fx.fsx_nextents, fx.fsx_projid, fx.fsx_cowextsize);
  } else if (strstart(&TT.fmt, "long}")) {
    long l;

    ptrace_struct(addr, &l, sizeof(l));
    fprintf(stderr, "%ld", l);
  } else if (strstart(&TT.fmt, "longx}")) {
    long l;

    ptrace_struct(addr, &l, sizeof(l));
    fprintf(stderr, "%#lx", l);
  } else if (strstart(&TT.fmt, "rlimit}")) {
    struct rlimit rl;

    ptrace_struct(addr, &rl, sizeof(rl));
    fprintf(stderr, "{rlim_cur=%lld, rlim_max=%lld}",
        (long long)rl.rlim_cur, (long long)rl.rlim_max);
  } else if (strstart(&TT.fmt, "sigset}")) {
    long long ss;
    int i;

    ptrace_struct(addr, &ss, sizeof(ss));
    fprintf(stderr, "[");
    for (i=0; i<64;++i) {
      // TODO: use signal names, fix spacing
      if (ss & (1ULL<<i)) fprintf(stderr, "%d ", i);
    }
    fprintf(stderr, "]");
  } else if (strstart(&TT.fmt, "stat}")) {
    struct stat sb;

    ptrace_struct(addr, &sb, sizeof(sb));
    // TODO: decode IFMT bits in st_mode
    if (FLAG(v)) {
      // TODO: full atime/mtime/ctime dump.
      fprintf(stderr, "{st_dev=makedev(%#x, %#x), st_ino=%ld, st_mode=%o, "
          "st_nlink=%ld, st_uid=%d, st_gid=%d, st_blksize=%ld, st_blocks=%ld, "
          "st_size=%lld, st_atime=%ld, st_mtime=%ld, st_ctime=%ld}",
          dev_major(sb.st_dev), dev_minor(sb.st_dev), sb.st_ino, sb.st_mode,
          sb.st_nlink, sb.st_uid, sb.st_gid, sb.st_blksize, sb.st_blocks,
          (long long)sb.st_size, sb.st_atime, sb.st_mtime, sb.st_ctime);
    } else {
      fprintf(stderr, "{st_mode=%o, st_size=%lld, ...}", sb.st_mode,
        (long long)sb.st_size);
    }
  } else if (strstart(&TT.fmt, "termios}")) {
    struct termios to;

    ptrace_struct(addr, &to, sizeof(to));
    fprintf(stderr, "{c_iflag=%#lx, c_oflag=%#lx, c_cflag=%#lx, c_lflag=%#lx}",
        (long)to.c_iflag, (long)to.c_oflag, (long)to.c_cflag, (long)to.c_lflag);
  } else if (strstart(&TT.fmt, "timespec}")) {
    struct timespec ts;

    ptrace_struct(addr, &ts, sizeof(ts));
    fprintf(stderr, "{tv_sec=%lld, tv_nsec=%lld}",
        (long long)ts.tv_sec, (long long)ts.tv_nsec);
  } else if (strstart(&TT.fmt, "winsize}")) {
    struct winsize ws;

    ptrace_struct(addr, &ws, sizeof(ws));
    fprintf(stderr, "{ws_row=%hu, ws_col=%hu, ws_xpixel=%hu, ws_ypixel=%hu}",
        ws.ws_row, ws.ws_col, ws.ws_xpixel, ws.ws_ypixel);
  } else abort();
}

static void print_ptr(long addr)
{
  if (!addr) fprintf(stderr, "NULL");
  else fprintf(stderr, "0x%lx", addr);
}

static void print_string(long addr)
{
  long offset = 0, total = 0;
  int done = 0, i;

  fputc('"', stderr);
  while (!done) {
    errno = 0;
    long v = ptrace(PTRACE_PEEKDATA, TT.pid, addr + offset);
    if (errno) return;
    memcpy(toybuf, &v, sizeof(v));
    for (i=0; i<sizeof(v); ++i) {
      if (!toybuf[i]) {
        // TODO: handle the case of dumping n bytes (e.g. read()/write()), not
        // just NUL-terminated strings.
        done = 1;
        break;
      }
      if (isprint(toybuf[i])) fputc(toybuf[i], stderr);
      else {
        // TODO: reuse an existing escape function.
        fputc('\\', stderr);
        if (toybuf[i] == '\n') fputc('n', stderr);
        else if (toybuf[i] == '\r') fputc('r', stderr);
        else if (toybuf[i] == '\t') fputc('t', stderr);
        else fprintf(stderr, "x%2.2x", toybuf[i]);
      }
      if (++total >= TT.s) {
        done = 1;
        break;
      }
    }
    offset += sizeof(v);
  }
  fputc('"', stderr);
}

static void print_bitmask(int bitmask, long v, char *zero, ...)
{
  va_list ap;
  int first = 1;

  if (!v && zero) {
    fprintf(stderr, "%s", zero);
    return;
  }

  va_start(ap, zero);
  for (;;) {
    int this = va_arg(ap, int);
    char *name;

    if (!this) break;
    name = va_arg(ap, char*);
    if (bitmask) {
      if (v & this) {
        fprintf(stderr, "%s%s", first?"":"|", name);
        first = 0;
        v &= ~this;
      }
    } else {
      if (v == this) {
        fprintf(stderr, "%s", name);
        v = 0;
        break;
      }
    }
  }
  va_end(ap);
  if (v) fprintf(stderr, "%s%#lx", first?"":"|", v);
}

static void print_flags(long v)
{
#define C(n) n, #n
  if (strstart(&TT.fmt, "access|")) {
    print_bitmask(1, v, "F_OK", C(R_OK), C(W_OK), C(X_OK), 0);
  } else if (strstart(&TT.fmt, "mmap|")) {
    print_bitmask(1, v, 0, C(MAP_SHARED), C(MAP_PRIVATE), C(MAP_32BIT),
        C(MAP_ANONYMOUS), C(MAP_FIXED), C(MAP_GROWSDOWN), C(MAP_HUGETLB),
        C(MAP_DENYWRITE), 0);
  } else if (strstart(&TT.fmt, "open|")) {
    print_bitmask(1, v, "O_RDONLY", C(O_WRONLY), C(O_RDWR), C(O_CLOEXEC),
        C(O_CREAT), C(O_DIRECTORY), C(O_EXCL), C(O_NOCTTY), C(O_NOFOLLOW),
        C(O_TRUNC), C(O_ASYNC), C(O_APPEND), C(O_DSYNC), C(O_EXCL),
        C(O_NOATIME), C(O_NONBLOCK), C(O_PATH), C(O_SYNC),
        0x4000, "O_DIRECT", 0x8000, "O_LARGEFILE", 0x410000, "O_TMPFILE", 0);
  } else if (strstart(&TT.fmt, "prot|")) {
    print_bitmask(1,v,"PROT_NONE",C(PROT_READ),C(PROT_WRITE),C(PROT_EXEC),0);
  } else abort();
}

static void print_alternatives(long v)
{
  if (strstart(&TT.fmt, "rlimit^")) {
    print_bitmask(0, v, "RLIMIT_CPU", C(RLIMIT_FSIZE), C(RLIMIT_DATA),
        C(RLIMIT_STACK), C(RLIMIT_CORE), C(RLIMIT_RSS), C(RLIMIT_NPROC),
        C(RLIMIT_NOFILE), C(RLIMIT_MEMLOCK), C(RLIMIT_AS), C(RLIMIT_LOCKS),
        C(RLIMIT_SIGPENDING), C(RLIMIT_MSGQUEUE), C(RLIMIT_NICE),
        C(RLIMIT_RTPRIO), C(RLIMIT_RTTIME), 0);
  } else if (strstart(&TT.fmt, "seek^")) {
    print_bitmask(0, v, "SEEK_SET", C(SEEK_CUR), C(SEEK_END), C(SEEK_DATA),
        C(SEEK_HOLE), 0);
  } else if (strstart(&TT.fmt, "sig^")) {
    print_bitmask(0, v, "SIG_BLOCK", C(SIG_UNBLOCK), C(SIG_SETMASK), 0);
  } else abort();
}

static void print_args()
{
  int i;

  // Loop through arguments and print according to format string
  for (i = 0; *TT.fmt; i++, TT.arg++) {
    long v = TT.regs[REG_ORDER[TT.arg]];
    char *s, ch;

    if (i) fprintf(stderr, ", ");
    switch (ch = *TT.fmt++) {
      case 'd': fprintf(stderr, "%ld", v); break; // decimal
      case 'f': if ((int) v == AT_FDCWD) fprintf(stderr, "AT_FDCWD");
                else fprintf(stderr, "%ld", v);
                break;
      case 'i': fprintf(stderr, "%s", strioctl(v)); break; // decimal
      case 'm': fprintf(stderr, "%03o", (unsigned) v); break; // mode for open()
      case 'o': fprintf(stderr, "%ld", v); break; // off_t
      case 'p': print_ptr(v); break;
      case 's': print_string(v); break;
      case 'S': // The libc-reserved signals aren't known to num_to_sig().
                // TODO: use an strace-only routine for >= 32?
                if (!(s = num_to_sig(v))) fprintf(stderr, "%ld", v);
                else fprintf(stderr, "SIG%s", s);
                break;
      case 'z': fprintf(stderr, "%zd", v); break; // size_t
      case 'x': fprintf(stderr, "%lx", v); break; // hex

      case '{': print_struct(v); break;
      case '|': print_flags(v); break;
      case '^': print_alternatives(v); break;

      case '/': return; // Separates "enter" and "exit" arguments.

      default: fprintf(stderr, "?%c<0x%lx>", ch, v); break;
    }
  }
}

static void print_enter(void)
{
  char *name;

  get_regs();
  TT.syscall = TT.regs[REG_ORDER[6]];
  if (TT.syscall == __NR_ioctl) {
    name = "ioctl";
    switch (TT.regs[REG_ORDER[1]]) {
      case FS_IOC_FSGETXATTR: TT.fmt = "fi/{fsxattr}"; break;
      case FS_IOC_FSSETXATTR: TT.fmt = "fi{fsxattr}"; break;
      case FS_IOC_GETFLAGS: TT.fmt = "fi/{longx}"; break;
      case FS_IOC_GETVERSION: TT.fmt = "fi/{long}"; break;
      case FS_IOC_SETFLAGS: TT.fmt = "fi{long}"; break;
      case FS_IOC_SETVERSION: TT.fmt = "fi{long}"; break;
      //case SIOCGIFCONF: struct ifconf
      case SIOCGIFADDR:
      case SIOCGIFBRDADDR:
      case SIOCGIFDSTADDR:
      case SIOCGIFFLAGS:
      case SIOCGIFHWADDR:
      case SIOCGIFMAP:
      case SIOCGIFMTU:
      case SIOCGIFNETMASK:
      case SIOCGIFTXQLEN: TT.fmt = "fi/{ifreq}"; break;
      case SIOCSIFADDR:
      case SIOCSIFBRDADDR:
      case SIOCSIFDSTADDR:
      case SIOCSIFFLAGS:
      case SIOCSIFHWADDR:
      case SIOCSIFMAP:
      case SIOCSIFMTU:
      case SIOCSIFNETMASK:
      case SIOCSIFTXQLEN: TT.fmt = "fi{ifreq}"; break;
      case TCGETS: TT.fmt = "fi/{termios}"; break;
      case TCSETS: TT.fmt = "fi{termios}"; break;
      case TIOCGWINSZ: TT.fmt = "fi/{winsize}"; break;
      case TIOCSWINSZ: TT.fmt = "fi{winsize}"; break;
      default:
        TT.fmt = (TT.regs[REG_ORDER[0]]&1) ? "fip" : "fi/p";
        break;
    }
  } else switch (TT.syscall) {
#define SC(n,f) case __NR_ ## n: name = #n; TT.fmt = f; break
    SC(access, "s|access|");
    SC(arch_prctl, "dp");
    SC(brk, "p");
    SC(close, "d");
    SC(connect, "fpd"); // TODO: sockaddr
    SC(dup, "f");
    SC(dup2, "ff");
    SC(dup3, "ff|open|");
    SC(execve, "spp");
    SC(exit_group, "d");
    SC(fcntl, "fdp"); // TODO: probably needs special case
    SC(fstat, "f/{stat}");
    SC(futex, "pdxppx");
    SC(getdents64, "dpz");
    SC(geteuid, "");
    SC(getuid, "");

    SC(getxattr, "sspz");
    SC(lgetxattr, "sspz");
    SC(fgetxattr, "fspz");

    SC(lseek, "fo^seek^");
    SC(lstat, "s/{stat}");
    SC(mmap, "pz|prot||mmap|fx");
    SC(mprotect, "pz|prot|");
    SC(mremap, "pzzdp"); // TODO: flags
    SC(munmap, "pz");
    SC(nanosleep, "{timespec}/{timespec}");
    SC(newfstatat, "fs/{stat}d");
    SC(open, "sd|open|m");
    SC(openat, "fs|open|m");
    SC(poll, "pdd");
    SC(prlimit64, "d^rlimit^{rlimit}/{rlimit}");
    SC(read, "d/sz");
    SC(readlinkat, "s/sz");
    SC(rt_sigaction, "Sppz");
    SC(rt_sigprocmask, "^sig^{sigset}/{sigset}z");
    SC(set_robust_list, "pd");
    SC(set_tid_address, "p");
    SC(socket, "ddd"); // TODO: flags
    SC(stat, "s/{stat}");
    SC(statfs, "sp");
    SC(sysinfo, "p");
    SC(umask, "m");
    SC(uname, "p");
    SC(write, "dsz");
    default:
      sprintf(name = toybuf, "SYS_%ld", TT.syscall);
      TT.fmt = "pppppp";
      break;
  }

  fprintf(stderr, "%s(", name);
  TT.arg = 0;
  print_args();
}

static void print_exit(void)
{
  long result;

  get_regs();
  result = TT.regs[REG_ORDER[7]];
  if (*TT.fmt) print_args();
  fprintf(stderr, ") = ");
  if (result >= -4095UL)
    fprintf(stderr, "-1 %s (%s)", strerrno(-result), strerror(-result));
  else if (TT.syscall==__NR_mmap || TT.syscall==__NR_brk) print_ptr(result);
  else fprintf(stderr, "%ld", result);
  fputc('\n', stderr);
}

static int next(void)
{
  int status;

  for (;;) {
    ptrace(PTRACE_SYSCALL, TT.pid, 0, 0);
    waitpid(TT.pid, &status, 0);
    // PTRACE_O_TRACESYSGOOD sets bit 7 to indicate a syscall.
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) return 1;
    if (WIFEXITED(status)) return 0;
    fprintf(stderr, "[stopped %d (%x)]\n", status, WSTOPSIG(status));
  }
}

static void strace_detach(int s)
{
  xptrace(PTRACE_DETACH, TT.pid, 0, 0);
  exit(1);
}

void strace_main(void)
{
  int status;

  if (!FLAG(s)) TT.s = 32;

  if (FLAG(p)) {
    if (*toys.optargs) help_exit("No arguments with -p");
    TT.pid = TT.p;
    signal(SIGINT, strace_detach);
    // TODO: PTRACE_SEIZE instead?
    xptrace(PTRACE_ATTACH, TT.pid, 0, 0);
  } else {
    if (!*toys.optargs) help_exit("Needs 1 argument");
    TT.pid = xfork();
    if (!TT.pid) {
      errno = 0;
      ptrace(PTRACE_TRACEME);
      if (errno) perror_exit("PTRACE_TRACEME failed");
      raise(SIGSTOP);
      toys.stacktop = 0;
      xexec(toys.optargs);
    }
  }

  do {
    waitpid(TT.pid, &status, 0);
  } while (!WIFSTOPPED(status));

  // TODO: PTRACE_O_TRACEEXIT
  // TODO: PTRACE_O_TRACEFORK/PTRACE_O_TRACEVFORK/PTRACE_O_TRACECLONE for -f.
  errno = 0;
  ptrace(PTRACE_SETOPTIONS, TT.pid, 0, PTRACE_O_TRACESYSGOOD);
  if (errno) perror_exit("PTRACE_SETOPTIONS PTRACE_O_TRACESYSGOOD failed");

  // TODO: real strace swallows the failed execve()s if it started the child

  for (;;) {
    if (!next()) break;
    print_enter();
    if (!next()) break;
    print_exit();
  }

  // TODO: support -f and keep track of children.
  waitpid(TT.pid, &status, 0);
  if (WIFEXITED(status))
    fprintf(stderr, "+++ exited with %d +++\n", WEXITSTATUS(status));
  if (WIFSTOPPED(status))
    fprintf(stderr, "+++ stopped with %d +++\n", WSTOPSIG(status));
}
