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
            [seek=N] [conv=notrunc|noerror|sync|fsync] [status=noxfer|none]

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
    status=noxfer Don't show transfer rate
    status=none   Don't show transfer rate or records in/out

    Numbers may be suffixed by c (*1), w (*2), b (*512), kD (*1000), k (*1024),
    MD (*1000*1000), M (*1024*1024), GD (*1000*1000*1000) or G (*1024*1024*1024).
*/

#define FOR_dd
#include "toys.h"

GLOBALS(
  int show_xfer;
  int show_records;
  unsigned long long bytes, c_count, in_full, in_part, out_full, out_part;
  struct timeval start;
  struct {
    char *name;
    int fd;
    unsigned char *buff, *bp;
    long sz, count;
    unsigned long long offset;
  } in, out;
);

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
#define C_STATUS  0x1000

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
  { "bs",      C_BS    },
  { "conv",    C_CONV  },
  { "count",   C_COUNT },
  { "ibs",     C_IBS   },
  { "if",      C_IF    },
  { "obs",     C_OBS   },
  { "of",      C_OF    },
  { "seek",    C_SEEK  },
  { "skip",    C_SKIP  },
  { "status",  C_STATUS},
};

static unsigned long long strsuftoll(char *arg, int def, unsigned long long max)
{
  unsigned long long result;
  char *p = arg;
  int i, idx = -1;

  while (isspace(*p)) p++;
  if (*p == '-') error_exit("invalid number '%s'", arg);

  errno = 0;
  result = strtoull(p, &p, 0);
  if (errno == ERANGE || result > max || result < def)
    perror_exit("invalid number '%s'", arg);
  if (*p != '\0') {
    for (i = 0; i < ARRAY_LEN(suffixes); i++)
      if (!strcmp(p, suffixes[i].name)) idx = i;
    if (idx == -1 || (max/suffixes[idx].val < result)) 
      error_exit("invalid number '%s'", arg);
    result *= suffixes[idx].val;
  }
  return result;
}

static void status()
{
  double seconds;
  struct timeval now;

  gettimeofday(&now, NULL);
  seconds = ((now.tv_sec * 1000000 + now.tv_usec) -
      (TT.start.tv_sec * 1000000 + TT.start.tv_usec))/1000000.0;

  if (TT.show_records)
    fprintf(stderr, "%llu+%llu records in\n%llu+%llu records out\n",
            TT.in_full, TT.in_part, TT.out_full, TT.out_part);

  if (TT.show_xfer) {
    human_readable(toybuf, TT.bytes, HR_SPACE|HR_B);
    fprintf(stderr, "%llu bytes (%s) copied, ", TT.bytes, toybuf);
    human_readable(toybuf, TT.bytes/seconds, HR_SPACE|HR_B);
    fprintf(stderr, "%f s, %s/s\n", seconds, toybuf);
  }
}

static void write_out(int all)
{
  TT.out.bp = TT.out.buff;
  while (TT.out.count) {
    ssize_t nw = writeall(TT.out.fd, TT.out.bp, ((all)? TT.out.count : TT.out.sz));

    all = 0; //further writes will be on obs
    if (nw <= 0) perror_exit("%s: write error", TT.out.name);
    if (nw == TT.out.sz) TT.out_full++;
    else TT.out_part++;
    TT.out.count -= nw;
    TT.out.bp += nw;
    TT.bytes += nw;
    if (TT.out.count < TT.out.sz) break;
  }
  if (TT.out.count) memmove(TT.out.buff, TT.out.bp, TT.out.count); //move remainder to front
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

  TT.show_xfer = TT.show_records = 1;

  TT.in.sz = TT.out.sz = 512; //default io block size
  while (*toys.optargs) {
    if (!(arg = strchr(*toys.optargs, '='))) error_exit("unknown arg %s", *toys.optargs);
    *arg++ = '\0';
    if (!*arg) help_exit(0);
    key.name = *toys.optargs;
    if (!(res = bsearch(&key, operands, ARRAY_LEN(operands), sizeof(struct pair),
            comp))) error_exit("unknown arg %s", key.name);

    toys.optflags |= res->val;
    switch (res->val) {
      case C_BS:
        TT.in.sz = TT.out.sz = strsuftoll(arg, 1, LONG_MAX);
        break;
      case C_IBS:
        sz = strsuftoll(arg, 1, LONG_MAX);
        if (!(toys.optflags & C_BS)) TT.in.sz = sz;
        break;
      case C_OBS:
        sz = strsuftoll(arg, 1, LONG_MAX);
        if (!(toys.optflags & C_BS)) TT.out.sz = sz;
        break;
      case C_COUNT:
        TT.c_count = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_IF:
        TT.in.name = arg;
        break;
      case C_OF:
        TT.out.name = arg;
        break;
      case C_SEEK:
        TT.out.offset = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_SKIP:
        TT.in.offset = strsuftoll(arg, 0, ULLONG_MAX);
        break;
      case C_STATUS:
        if (!strcmp(arg, "noxfer")) TT.show_xfer = 0;
        else if (!strcmp(arg, "none")) TT.show_xfer = TT.show_records = 0;
        else error_exit("unknown status '%s'", arg);
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

  signal(SIGINT, generic_signal);
  signal(SIGUSR1, generic_signal);
  gettimeofday(&TT.start, NULL);

  /* for C_BS, in/out is done as it is. so only in.sz is enough.
   * With Single buffer there will be overflow in a read following partial read
   */
  TT.in.buff = TT.out.buff = xmalloc(TT.in.sz
    + ((toys.optflags & C_BS) ? 0 : TT.out.sz));
  TT.in.bp = TT.out.bp = TT.in.buff;
  //setup input
  if (!TT.in.name) TT.in.name = "stdin";
  else TT.in.fd = xopenro(TT.in.name);

  //setup output
  if (!TT.out.name) {
    TT.out.name = "stdout";
    TT.out.fd = 1;
  } else TT.out.fd = xcreate(TT.out.name,
      O_WRONLY|O_CREAT|(O_TRUNC*!(toys.optflags&C_NOTRUNC)), 0666);

  // Implement skip=
  if (TT.in.offset) {
    if (lseek(TT.in.fd, (off_t)(TT.in.offset * TT.in.sz), SEEK_CUR) < 0) {
      while (TT.in.offset--) {
        ssize_t n = read(TT.in.fd, TT.in.bp, TT.in.sz);

        if (n < 0) {
          perror_msg("%s", TT.in.name);
          if (toys.optflags & C_NOERROR) status();
          else return;
        } else if (!n) {
          xprintf("%s: Can't skip\n", TT.in.name);
          return;
        }
      }
    }
  }

  if (TT.out.offset)
    xlseek(TT.out.fd, TT.out.offset * (off_t)TT.out.sz, SEEK_CUR);

  if ((toys.optflags&C_SEEK) && !(toys.optflags & C_NOTRUNC))
    if (ftruncate(TT.out.fd, TT.out.offset * TT.out.sz))
      perror_exit("ftruncate");

  while (!(toys.optflags & C_COUNT) || (TT.in_full + TT.in_part) < TT.c_count) {
    ssize_t n;

    // Show progress and exit on SIGINT or just continue on SIGUSR1.
    if (toys.signal) {
      status();
      if (toys.signal==SIGINT) exit_signal(toys.signal);
      toys.signal = 0;
    }

    TT.in.bp = TT.in.buff + TT.in.count;
    if (toys.optflags & C_SYNC) memset(TT.in.bp, 0, TT.in.sz);
    if (!(n = read(TT.in.fd, TT.in.bp, TT.in.sz))) break;
    if (n < 0) { 
      if (errno == EINTR) continue;
      //read error case.
      perror_msg("%s: read error", TT.in.name);
      if (!(toys.optflags & C_NOERROR)) exit(1);
      status();
      xlseek(TT.in.fd, TT.in.sz, SEEK_CUR);
      if (!(toys.optflags & C_SYNC)) continue;
      // if SYNC, then treat as full block of nuls
      n = TT.in.sz;
    }
    if (n == TT.in.sz) {
      TT.in_full++;
      TT.in.count += n;
    } else {
      TT.in_part++;
      if (toys.optflags & C_SYNC) TT.in.count += TT.in.sz;
      else TT.in.count += n;
    }

    TT.out.count = TT.in.count;
    if (toys.optflags & C_BS) {
      write_out(1);
      TT.in.count = 0;
      continue;
    }

    if (TT.in.count >= TT.out.sz) {
      write_out(0);
      TT.in.count = TT.out.count;
    }
  }
  if (TT.out.count) write_out(1); //write any remaining input blocks
  if (toys.optflags & C_FSYNC && fsync(TT.out.fd) < 0) 
    perror_exit("%s: fsync fail", TT.out.name);

  close(TT.in.fd);
  close(TT.out.fd);
  if (TT.in.buff) free(TT.in.buff);

  status();
}
