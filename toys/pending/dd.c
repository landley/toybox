/* dd.c - program to convert and copy a file.
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See  http://opengroup.org/onlinepubs/9699919799/utilities/dd.html

USE_DD(NEWTOY(dd, 0, TOYFLAG_USR|TOYFLAG_BIN))

config DD
  bool "dd"
  default n
  help
    usage: dd [if=FILE] [of=FILE] [ibs=N] [obs=N] [iflag=FLAGS] [oflag=FLAGS]
            [bs=N] [count=N] [seek=N] [skip=N]
            [conv=notrunc|noerror|sync|fsync] [status=noxfer|none]

    Copy/convert files.

    if=FILE		Read from FILE instead of stdin
    of=FILE		Write to FILE instead of stdout
    bs=N		Read and write N bytes at a time
    ibs=N		Input block size
    obs=N		Output block size
    count=N		Copy only N input blocks
    skip=N		Skip N input blocks
    seek=N		Skip N output blocks
    iflag=FLAGS	Set input flags
    oflag=FLAGS	Set output flags
    conv=notrunc	Don't truncate output file
    conv=noerror	Continue after read errors
    conv=sync	Pad blocks with zeros
    conv=fsync	Physically write data out before finishing
    status=noxfer	Don't show transfer rate
    status=none	Don't show transfer rate or records in/out

    FLAGS is a comma-separated list of:

    count_bytes	(iflag) interpret count=N in bytes, not blocks
    seek_bytes	(oflag) interpret seek=N in bytes, not blocks
    skip_bytes	(iflag) interpret skip=N in bytes, not blocks

    Numbers may be suffixed by c (*1), w (*2), b (*512), kD (*1000), k (*1024),
    MD (*1000*1000), M (*1024*1024), GD (*1000*1000*1000) or G (*1024*1024*1024).
*/

#define FOR_dd
#include "toys.h"

GLOBALS(
  int show_xfer, show_records;
  unsigned long long bytes, c_count, in_full, in_part, out_full, out_part;
  struct timeval start;
  struct {
    char *name;
    int fd;
    unsigned char *buff, *bp;
    long sz, count;
    unsigned long long offset;
  } in, out;
  unsigned conv, iflag, oflag;
);

struct dd_flag {
  char *name;
};

static const struct dd_flag dd_conv[] = TAGGED_ARRAY(DD_conv,
  {"fsync"}, {"noerror"}, {"notrunc"}, {"sync"},
);

static const struct dd_flag dd_iflag[] = TAGGED_ARRAY(DD_iflag,
  {"count_bytes"}, {"skip_bytes"},
);

static const struct dd_flag dd_oflag[] = TAGGED_ARRAY(DD_oflag,
  {"seek_bytes"},
);

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

static void dd_sigint(int sig) {
  status();
  toys.exitval = sig|128;
  xexit();
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

static void parse_flags(char *what, char *arg,
    const struct dd_flag* flags, int flag_count, unsigned *result)
{
  char *pre = xstrdup(arg);
  int i;

  for (i=0; i<flag_count; ++i) {
    while (comma_remove(pre, flags[i].name)) *result |= 1<<i;
  }
  if (*pre) error_exit("bad %s=%s", what, pre);
  free(pre);
}

void dd_main()
{
  char **args;
  unsigned long long bs = 0;
  int trunc = O_TRUNC;

  TT.show_xfer = TT.show_records = 1;
  TT.c_count = ULLONG_MAX;

  TT.in.sz = TT.out.sz = 512; //default io block size
  for (args = toys.optargs; *args; args++) {
    char *arg = *args;

    if (strstart(&arg, "bs=")) bs = atolx_range(arg, 1, LONG_MAX);
    else if (strstart(&arg, "ibs=")) TT.in.sz = atolx_range(arg, 1, LONG_MAX);
    else if (strstart(&arg, "obs=")) TT.out.sz = atolx_range(arg, 1, LONG_MAX);
    else if (strstart(&arg, "count="))
      TT.c_count = atolx_range(arg, 0, LLONG_MAX);
    else if (strstart(&arg, "if=")) TT.in.name = arg;
    else if (strstart(&arg, "of=")) TT.out.name = arg;
    else if (strstart(&arg, "seek="))
      TT.out.offset = atolx_range(arg, 0, LLONG_MAX);
    else if (strstart(&arg, "skip="))
      TT.in.offset = atolx_range(arg, 0, LLONG_MAX);
    else if (strstart(&arg, "status=")) {
      if (!strcmp(arg, "noxfer")) TT.show_xfer = 0;
      else if (!strcmp(arg, "none")) TT.show_xfer = TT.show_records = 0;
      else error_exit("unknown status '%s'", arg);
    } else if (strstart(&arg, "conv=")) {
      parse_flags("conv", arg, dd_conv, ARRAY_LEN(dd_conv), &TT.conv);
      fprintf(stderr, "conv=%x\n", TT.conv);
    } else if (strstart(&arg, "iflag="))
      parse_flags("iflag", arg, dd_iflag, ARRAY_LEN(dd_iflag), &TT.iflag);
    else if (strstart(&arg, "oflag="))
      parse_flags("oflag", arg, dd_oflag, ARRAY_LEN(dd_oflag), &TT.oflag);
    else error_exit("bad arg %s", arg);
  }
  if (bs) TT.in.sz = TT.out.sz = bs;

  signal(SIGINT, dd_sigint);
  signal(SIGUSR1, generic_signal);
  gettimeofday(&TT.start, NULL);

  // For bs=, in/out is done as it is. so only in.sz is enough.
  // With Single buffer there will be overflow in a read following partial read.
  TT.in.buff = TT.out.buff = xmalloc(TT.in.sz + (bs ? 0 : TT.out.sz));
  TT.in.bp = TT.out.bp = TT.in.buff;

  if (!TT.in.name) TT.in.name = "stdin";
  else TT.in.fd = xopenro(TT.in.name);

  if (TT.conv & _DD_conv_notrunc) trunc = 0;

  if (!TT.out.name) {
    TT.out.name = "stdout";
    TT.out.fd = 1;
  } else TT.out.fd = xcreate(TT.out.name,
    O_WRONLY|O_CREAT|(trunc*!TT.out.offset), 0666);

  // Implement skip=
  if (TT.in.offset) {
    off_t off = TT.in.offset;

    if (!(TT.iflag & _DD_iflag_skip_bytes)) off *= TT.in.sz;
    if (lseek(TT.in.fd, off, SEEK_CUR) < 0) {
      while (off > 0) {
        int chunk = off < TT.in.sz ? off : TT.in.sz;
        ssize_t n = read(TT.in.fd, TT.in.bp, chunk);

        if (n < 0) {
          perror_msg("%s", TT.in.name);
          if (TT.conv & _DD_conv_noerror) status();
          else return;
        } else if (!n) {
          xprintf("%s: Can't skip\n", TT.in.name);
          return;
        }
        off -= n;
      }
    }
  }

  // Implement seek= and truncate as necessary. We handled position zero
  // truncate with O_TRUNC on open, so output to /dev/null and such doesn't
  // error.
  bs = TT.out.offset;
  if (!(TT.oflag & _DD_oflag_seek_bytes)) bs *= TT.out.sz;
  if (bs) {
    xlseek(TT.out.fd, bs, SEEK_CUR);
    if (trunc && ftruncate(TT.out.fd, bs)) perror_exit("ftruncate");
  }

  unsigned long long bytes_left = TT.c_count;
  if (TT.c_count != ULLONG_MAX && !(TT.iflag & _DD_iflag_count_bytes)) {
    bytes_left *= TT.in.sz;
  }
  while (bytes_left) {
    int chunk = bytes_left < TT.in.sz ? bytes_left : TT.in.sz;
    ssize_t n;

    // Show progress and exit on SIGINT or just continue on SIGUSR1.
    if (toys.signal) {
      status();
      if (toys.signal==SIGINT) exit_signal(toys.signal);
      toys.signal = 0;
    }

    TT.in.bp = TT.in.buff + TT.in.count;
    if (TT.conv & _DD_conv_sync) memset(TT.in.bp, 0, TT.in.sz);
    if (!(n = read(TT.in.fd, TT.in.bp, chunk))) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      //read error case.
      perror_msg("%s: read error", TT.in.name);
      if (!(TT.conv & _DD_conv_noerror)) exit(1);
      status();
      xlseek(TT.in.fd, TT.in.sz, SEEK_CUR);
      if (!(TT.conv & _DD_conv_sync)) continue;
      // if SYNC, then treat as full block of nuls
      n = TT.in.sz;
    }
    if (n == TT.in.sz) {
      TT.in_full++;
      TT.in.count += n;
    } else {
      TT.in_part++;
      if (TT.conv & _DD_conv_sync) TT.in.count += TT.in.sz;
      else TT.in.count += n;
    }
    bytes_left -= n;

    TT.out.count = TT.in.count;
    if (bs) {
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
  if ((TT.conv & _DD_conv_fsync) && fsync(TT.out.fd)<0)
    perror_exit("%s: fsync", TT.out.name);

  close(TT.in.fd);
  close(TT.out.fd);
  if (TT.in.buff) free(TT.in.buff);

  status();
}
