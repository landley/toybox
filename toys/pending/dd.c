/* dd.c - program to convert and copy a file.
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See  http://opengroup.org/onlinepubs/9699919799/utilities/dd.html
USE_DD(NEWTOY(dd, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config DD
  bool "dd"
  default n
    help
    usage: dd [if=FILE] [of=FILE] [ibs=N] [obs=N] [bs=N] [count=N] [skip=N]
            [seek=N] [conv=notrunc|noerror|sync|fsync]

    Options:
    if=FILE   Read from FILE instead of stdin
    of=FILE   Write to FILE instead of stdout
    bs=N      Read and write N bytes at a time
    ibs=N     Read N bytes at a time
    obs=N     Write N bytes at a time
    count=N   Copy only N input blocks
    skip=N    Skip N input blocks
    seek=N    Skip N output blocks
    conv=notrunc  Don't truncate output file
    conv=noerror  Continue after read errors
    conv=sync     Pad blocks with zeros
    conv=fsync    Physically write data out before finishing

    Numbers may be suffixed by c (x1), w (x2), b (x512), kD (x1000), k (x1024),
    MD (x1000000), M (x1048576), GD (x1000000000) or G (x1073741824)
    Copy a file, converting and formatting according to the operands.
*/
#define FOR_dd
#include "toys.h"

GLOBALS(
  int sig;
)
#define C_CONV    0x0000
#define C_BS      0x0001
#define C_COUNT   0x0002
#define C_IBS     0x0004
#define C_OBS     0x0008
#define C_IF      0x0010
#define C_OF      0x0020
#define C_SEEK    0x0040
#define C_SKIP    0x0080
#define C_SYNC    0x0100
#define C_FSYNC   0x0200
#define C_NOERROR 0x0400
#define C_NOTRUNC 0x0800

struct io {
  char *name;
  int fd;
  unsigned char *buff, *bp;
  long sz, count;
  unsigned long long offset;
};

struct iostat {
  unsigned long long in_full, in_part, out_full, out_part, bytes;
  struct timeval start;
};

struct pair {
  char *name;
  unsigned val;
};

static struct pair suffixes[] = {
  { "c", 1 }, { "w", 2 }, { "b", 512 },
  { "kD", 1000 }, { "k", 1024 }, { "K", 1024 },
  { "MD", 1000000 }, { "M", 1048576 },
  { "GD", 1000000000 }, { "G", 1073741824 }
};

static struct pair clist[] = {
  { "fsync",    C_FSYNC },
  { "noerror",  C_NOERROR },
  { "notrunc",  C_NOTRUNC },
  { "sync",     C_SYNC },
};

static struct pair operands[] = {
  // keep the array sorted by name, bsearch() can be used.
  { "bs",    C_BS   },
  { "conv",  C_CONV },
  { "count", C_COUNT},
  { "ibs",   C_IBS  },
  { "if",    C_IF   },
  { "obs",   C_OBS  },
  { "of",    C_OF   },
  { "seek",  C_SEEK },
  { "skip",  C_SKIP },
};

static struct io in, out;
static struct iostat st;
static unsigned long long c_count;

static unsigned long long strsuftoll(char* arg, int def, unsigned long long max)
{
  unsigned long long result;
  char *endp, *ch = arg;
  int i, idx = -1;
  errno = 0;

  while (isspace(*ch)) ch++;
  if (*ch == '-') error_exit("invalid number '%s'",arg);
  result = strtoull(arg, &endp, 10);
  if (errno == ERANGE || result > max || result < def)
    perror_exit("invalid number '%s'",arg);
  if (*endp != '\0') {
    for (i = 0; i < ARRAY_LEN(suffixes); i++)
      if (!strcmp(endp, suffixes[i].name)) idx = i;
    if (idx == -1 || (max/suffixes[idx].val < result)) 
      error_exit("invalid number '%s'",arg);
    result = result* suffixes[idx].val;
  }
  return result;
}

static void summary()
{
  double seconds = 5.0;
  struct timeval now;

  gettimeofday(&now, NULL);
  seconds = ((now.tv_sec * 1000000 + now.tv_usec) - (st.start.tv_sec * 1000000
        + st.start.tv_usec))/1000000.0;
  //out to STDERR
  fprintf(stderr,"%llu+%llu records in\n%llu+%llu records out\n", st.in_full, st.in_part,
      st.out_full, st.out_part);
  human_readable(toybuf, st.bytes);
  fprintf(stderr, "%llu bytes (%s) copied, ",st.bytes, toybuf);
  human_readable(toybuf, st.bytes/seconds);
  fprintf(stderr, "%f s, %s/s\n", seconds, toybuf);
}

static void sig_handler(int sig)
{
  TT.sig = sig;
}

static int xmove_fd(int fd)
{
  int newfd;

  if (fd > STDERR_FILENO) return fd;
  if ((newfd = fcntl(fd, F_DUPFD, 3) < 0)) perror_exit("dupfd IO");
  close(fd);
  return newfd;
}

static void setup_inout()
{
  ssize_t n;

  /* for C_BS, in/out is done as it is. so only in.sz is enough.
   * With Single buffer there will be overflow in a read following partial read
   */
  in.buff = out.buff = xmalloc(in.sz + ((toys.optflags & C_BS)? 0: out.sz));
  in.bp = out.bp = in.buff;
  atexit(summary);
  //setup input
  if (!in.name) {
    in.name = "stdin";
    in.fd = STDIN_FILENO;
  } else {
    in.fd = xopen(in.name, O_RDONLY);
    in.fd = xmove_fd(in.fd);
  }
  //setup outout
  if (!out.name) {
    out.name = "stdout";
    out.fd = STDOUT_FILENO;
  } else {
    out.fd = xcreate(out.name, O_WRONLY | O_CREAT, 0666);
    out.fd = xmove_fd(out.fd);
  }

  if (in.offset) {
    if (lseek(in.fd, (off_t)(in.offset * in.sz), SEEK_CUR) < 0) {
      while (in.offset--) {
        if ((n = read(in.fd, in.bp, in.sz)) < 0) {
          if (toys.optflags & C_NOERROR) { //warn message and summary
            error_msg("%s: read error", in.name);
            summary();
          } else perror_exit("%s: read error", in.name);
        } else if (!n) {
          xprintf("%s: Can't skip\n", in.name);
          exit(0);
        }
      }
    }
  }

  if (out.offset) xlseek(out.fd, (off_t)(out.offset * out.sz), SEEK_CUR);
}

static void write_out(int all)
{
  ssize_t nw;
  out.bp = out.buff;
  while (out.count) {
    nw = writeall(out.fd, out.bp, ((all)? out.count : out.sz));
    all = 0; //further writes will be on obs
    if (nw <= 0) perror_exit("%s: write error",out.name);
    if (nw == out.sz) st.out_full++;
    else st.out_part++;
    out.count -= nw;
    out.bp += nw;
    st.bytes += nw;
    if (out.count < out.sz) break;
  }
  if (out.count) memmove(out.buff, out.bp, out.count); //move remainder to front
}

static void do_dd(void)
{
  ssize_t n;
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sig_handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  setup_inout();
  gettimeofday(&st.start, NULL);

  if (toys.optflags & (C_OF | C_SEEK) && !(toys.optflags & C_NOTRUNC))
    ftruncate(out.fd, (off_t)out.offset * out.sz);

  while (!(toys.optflags & C_COUNT) || (st.in_full + st.in_part) < c_count) {
    if (TT.sig == SIGUSR1) {
      summary();
      TT.sig = 0;
    } else if (TT.sig == SIGINT) exit(TT.sig | 128);
    in.bp = in.buff + in.count;
    if (toys.optflags & C_SYNC) memset(in.bp, 0, in.sz);
    if (!(n = read(in.fd, in.bp, in.sz))) break;
    if (n < 0) { 
      if (errno == EINTR) continue;
      //read error case.
      perror_msg("%s: read error", in.name);
      if (!(toys.optflags & C_NOERROR)) exit(1);
      summary();
      xlseek(in.fd, in.sz, SEEK_CUR);
      if (!(toys.optflags & C_SYNC)) continue;
      // if SYNC, then treat as full block of nuls
      n = in.sz;
    }
    if (n == in.sz) {
      st.in_full++;
      in.count += n;
    } else {
      st.in_part++;
      if (toys.optflags & C_SYNC) in.count += in.sz;
      else in.count += n;
    }

    out.count = in.count;
    if (toys.optflags & C_BS) {
      write_out(1);
      in.count = 0;
      continue;
    }

    if (in.count >= out.sz) {
      write_out(0);
      in.count = out.count;
    }
  }
  if (out.count) write_out(1); //write any remaining input blocks
  if (toys.optflags & C_FSYNC && fsync(out.fd) < 0) 
    perror_exit("%s: fsync fail", out.name);

  close(in.fd);
  close(out.fd);
  if (in.buff) free(in.buff);
}

static int comp(const void *a, const void *b) //const to shut compiler up
{
  return strcmp(((struct pair*)a)->name, ((struct pair*)b)->name);
}

void dd_main()
{
  struct pair *res, key;
  char *arg;
  long sz;

  in.sz = out.sz = 512; //default io block size
  while (*toys.optargs) {
    if (!(arg = strchr(*toys.optargs, '='))) error_exit("unknown arg %s", *toys.optargs);
    *arg++ = '\0';
    if (!*arg) {
      toys.exithelp = 1;
      error_exit("");
    }
    key.name = *toys.optargs;
    if (!(res = bsearch(&key, operands, ARRAY_LEN(operands), sizeof(struct pair),
            comp))) error_exit("unknown arg %s", key.name);

    toys.optflags |= res->val;
    switch(res->val) {
      case C_BS:
        in.sz = out.sz = strsuftoll(arg, 1, LONG_MAX);
        break;
      case C_IBS:
        sz = strsuftoll(arg, 1, LONG_MAX);
        if (!(toys.optflags & C_BS)) in.sz = sz;
        break;
      case C_OBS:
        sz = strsuftoll(arg, 1, LONG_MAX);
        if (!(toys.optflags & C_BS)) out.sz = sz;
        break;
      case C_COUNT:
        c_count = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_IF:
        in.name = arg;
        break;
      case C_OF:
        out.name = arg;
        break;
      case C_SEEK:
        out.offset = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_SKIP:
        in.offset = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_CONV:
        while (arg) {
          key.name = strsep(&arg, ",");
          if (!(res = bsearch(&key, clist, ARRAY_LEN(clist), 
                  sizeof(struct pair), comp)))
            error_exit("unknown conversion %s", key.name);

          toys.optflags |= res->val;
        }            
        break;
    }
    toys.optargs++;
  }

  do_dd();
}
