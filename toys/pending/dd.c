/* dd.c - program to convert and copy a file.
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See  http://opengroup.org/onlinepubs/9699919799/utilities/dd.html
 *
 * todo: ctrl-c doesn't work, the read() is restarting.

USE_DD(NEWTOY(dd, 0, TOYFLAG_USR|TOYFLAG_BIN))

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
);

#define C_FSYNC   1
#define C_NOERROR 2
#define C_NOTRUNC 4
#define C_SYNC    8

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

int strstarteq(char **a, char *b)
{
  char *aa = *a;

  return strstart(&aa, b) && *aa == '=' && (*a = aa+1);
}

void dd_main()
{
  char **args;
  unsigned long long bs = 0;
  int trunc = O_TRUNC, conv = 0;

  TT.show_xfer = TT.show_records = 1;
  TT.c_count = ULLONG_MAX;

  TT.in.sz = TT.out.sz = 512; //default io block size
  for (args = toys.optargs; *args; args++) {
    char *arg = *args;

    if (strstarteq(&arg, "bs")) bs = atolx_range(arg, 1, LONG_MAX);
    else if (strstarteq(&arg, "ibs")) TT.in.sz = atolx_range(arg, 1, LONG_MAX);
    else if (strstarteq(&arg, "obs")) TT.out.sz = atolx_range(arg, 1, LONG_MAX);
    else if (strstarteq(&arg, "count"))
      TT.c_count = atolx_range(arg, 0, LLONG_MAX);
    else if (strstarteq(&arg, "if")) TT.in.name = arg;
    else if (strstarteq(&arg, "of")) TT.out.name = arg;
    else if (strstarteq(&arg, "seek"))
      TT.out.offset = atolx_range(arg, 0, LLONG_MAX);
    else if (strstarteq(&arg, "skip"))
      TT.in.offset = atolx_range(arg, 0, LLONG_MAX);
    else if (strstarteq(&arg, "status")) {
      if (!strcmp(arg, "noxfer")) TT.show_xfer = 0;
      else if (!strcmp(arg, "none")) TT.show_xfer = TT.show_records = 0;
      else error_exit("unknown status '%s'", arg);
    } else if (strstarteq(&arg, "conv")) {
      char *ss, *convs[] = {"fsync", "noerror", "notrunc", "sync"};
      int i, len;

      while ((ss = comma_iterate(&arg, &len))) {
        for (i = 0; i<ARRAY_LEN(convs); i++)
          if (len == strlen(convs[i]) && !strncmp(ss, convs[i], len)) break;
        if (i == ARRAY_LEN(convs)) error_exit("bad conv=%.*s", len, ss);
        conv |= 1<<i;
      }
    } else error_exit("bad arg %s", arg);
  }
  if (bs) TT.in.sz = TT.out.sz = bs;

  signal(SIGINT, generic_signal);
  signal(SIGUSR1, generic_signal);
  gettimeofday(&TT.start, NULL);

  /* for bs=, in/out is done as it is. so only in.sz is enough.
   * With Single buffer there will be overflow in a read following partial read
   */
  TT.in.buff = TT.out.buff = xmalloc(TT.in.sz + (bs ? 0 : TT.out.sz));
  TT.in.bp = TT.out.bp = TT.in.buff;
  //setup input
  if (!TT.in.name) TT.in.name = "stdin";
  else TT.in.fd = xopenro(TT.in.name);

  if (conv&C_NOTRUNC) trunc = 0;

  //setup output
  if (!TT.out.name) {
    TT.out.name = "stdout";
    TT.out.fd = 1;
  } else TT.out.fd = xcreate(TT.out.name,
    O_WRONLY|O_CREAT|(trunc*!TT.out.offset), 0666);

  // Implement skip=
  if (TT.in.offset) {
    if (lseek(TT.in.fd, (off_t)(TT.in.offset * TT.in.sz), SEEK_CUR) < 0) {
      while (TT.in.offset--) {
        ssize_t n = read(TT.in.fd, TT.in.bp, TT.in.sz);

        if (n < 0) {
          perror_msg("%s", TT.in.name);
          if (conv&C_NOERROR) status();
          else return;
        } else if (!n) {
          xprintf("%s: Can't skip\n", TT.in.name);
          return;
        }
      }
    }
  }

  // seek/truncate as necessary. We handled position zero truncate with
  // O_TRUNC on open, so output to /dev/null and such doesn't error.
  if (TT.out.fd!=1 && (bs = TT.out.offset*TT.out.sz)) {
    xlseek(TT.out.fd, bs, SEEK_CUR);
    if (trunc && ftruncate(TT.out.fd, bs)) perror_exit("ftruncate");
  }

  while (TT.c_count==ULLONG_MAX || (TT.in_full + TT.in_part) < TT.c_count) {
    ssize_t n;

    // Show progress and exit on SIGINT or just continue on SIGUSR1.
    if (toys.signal) {
      status();
      if (toys.signal==SIGINT) exit_signal(toys.signal);
      toys.signal = 0;
    }

    TT.in.bp = TT.in.buff + TT.in.count;
    if (conv&C_SYNC) memset(TT.in.bp, 0, TT.in.sz);
    if (!(n = read(TT.in.fd, TT.in.bp, TT.in.sz))) break;
    if (n < 0) { 
      if (errno == EINTR) continue;
      //read error case.
      perror_msg("%s: read error", TT.in.name);
      if (!(conv&C_NOERROR)) exit(1);
      status();
      xlseek(TT.in.fd, TT.in.sz, SEEK_CUR);
      if (!(conv&C_SYNC)) continue;
      // if SYNC, then treat as full block of nuls
      n = TT.in.sz;
    }
    if (n == TT.in.sz) {
      TT.in_full++;
      TT.in.count += n;
    } else {
      TT.in_part++;
      if (conv&C_SYNC) TT.in.count += TT.in.sz;
      else TT.in.count += n;
    }

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
  if ((conv&C_FSYNC) && fsync(TT.out.fd)<0)
    perror_exit("%s: fsync", TT.out.name);

  close(TT.in.fd);
  close(TT.out.fd);
  if (TT.in.buff) free(TT.in.buff);

  status();
}
