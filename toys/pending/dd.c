/* dd.c - convert/copy a file
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/dd.html
 *
 * Deviations from posix: no conversions, no cbs=
 * TODO: seek=n with unseekable output? (Read output and... write it back?)

USE_DD(NEWTOY(dd, 0, TOYFLAG_USR|TOYFLAG_BIN))

config DD
  bool "dd"
  default n
  help
    usage: dd [if|of=FILE] [ibs|obs|bs|count|seek|skip=N] [conv|status|iflag|oflag=FLAG[,FLAG...]]

    Copy/convert blocks of data from input to output, with the following
    keyword=value modifiers (and their default values):

    if=FILE  Read FILE (stdin)          of=FILE  Write to FILE (stdout)
       bs=N  Block size in bytes (512)  count=N  Stop after copying N blocks (all)
      ibs=N  Input block size (bs=)       obs=N  Output block size (bs=)
     skip=N  Skip N input blocks (0)     seek=N  Skip N output blocks (0)

    Each =N value accepts the normal unit suffixes (see toybox --help).

    These modifiers take a comma separated list of potential options:

    iflag=count_bytes,skip_bytes   count=N or skip=N is in bytes not blocks
    oflag=seek_bytes,append        seek=N is in bytes, append output to file
    status=noxfer,none             don't show transfer rate, no summary info
    conv=
      notrunc  Don't truncate output    noerror  Continue after read errors
      sync     Zero pad short reads     fsync    Flush output to disk at end
      sparse   Seek past zeroed output  excl     Fail if output file exists
      nocreat  Fail if of=FILE missing
*/

#define FOR_dd
#include "toys.h"

GLOBALS(
  int show_xfer, show_records;
  unsigned long long bytes, in_full, in_part, out_full, out_part, start;
  char *buff;
  struct {
    char *name, *bp;
    int fd;
    unsigned long long sz, count;
  } in, out;
  unsigned conv, iflag, oflag;
)

struct dd_flag {
  char *name;
};

static const struct dd_flag dd_conv[] = TAGGED_ARRAY(DD_conv,
  {"fsync"}, {"noerror"}, {"notrunc"}, {"sync"}, // TODO sparse excl nocreat
);

static const struct dd_flag dd_iflag[] = TAGGED_ARRAY(DD_iflag,
  {"count_bytes"}, {"skip_bytes"},
);

static const struct dd_flag dd_oflag[] = TAGGED_ARRAY(DD_oflag,
  {"seek_bytes"},
);

static void status()
{
  unsigned long long now = millitime()-TT.start ? : 1, bytes = TT.bytes*1000;

  if (TT.show_records)
    fprintf(stderr, "%llu+%llu records in\n%llu+%llu records out\n",
            TT.in_full, TT.in_part, TT.out_full, TT.out_part);

  if (TT.show_xfer) {
    human_readable(toybuf, TT.bytes, HR_SPACE|HR_B);
    fprintf(stderr, "%llu bytes (%s) copied, ", TT.bytes, toybuf);
    bytes = (bytes>TT.bytes) ? bytes/now : TT.bytes/((now+999)/1000);
    human_readable(toybuf, bytes, HR_SPACE|HR_B);
    fprintf(stderr, "%llu.%03u s, %s/s\n", now/1000, (int)(now%1000), toybuf);
  }
}

static void write_out(int all)
{
  TT.out.bp = TT.buff;
  while (TT.out.count) {
    // Posix says a bunch about short reads... and didn't think of short writes.
    ssize_t nw = writeall(TT.out.fd, TT.out.bp, all ? TT.out.count : TT.out.sz);

    all = 0; //further writes will be on obs
    if (nw <= 0) perror_exit("%s: write error", TT.out.name);
    if (nw == TT.out.sz) TT.out_full++;
    else TT.out_part++;
    TT.out.count -= nw;
    TT.out.bp += nw;
    TT.bytes += nw;
    if (TT.out.count < TT.out.sz) break;
  }
  // move remainder to front (if any)
  if (TT.out.count) memmove(TT.buff, TT.out.bp, TT.out.count);
}

static void parse_flags(char *what, char *arg,
    const struct dd_flag* flags, int flag_count, unsigned *result)
{
  char *pre = xstrdup(arg);
  int i;

  for (i = 0; i<flag_count; ++i)
    while (comma_remove(pre, flags[i].name)) *result |= 1<<i;
  if (*pre) error_exit("bad %s=%s", what, pre);
  free(pre);
}

// Multiply detecting overflow.
static unsigned long long overmul(unsigned long long x, unsigned long long y)
{
  unsigned long long ll = x*y;

  if (x && y && (ll<x || ll<y)) error_exit("overflow");

  return ll;
}

// Handle funky posix 1x2x3 syntax.
static unsigned long long argxarg(char *arg, int cap)
{
  long long ll = 1;
  char x, *s, *new;

  arg = xstrdup(arg);
  for (new = s = arg;; new = s+1) {
    // atolx() handlex 0x hex prefixes, so skip past those looking for separator
    if ((s = strchr(new+2*!strncmp(new, "0x", 2), 'x'))) {
      if (s==new) break;
      x = *s;
      *s = 0;
    }
    ll = overmul(ll, atolx(new));
    if (s) *s = x;
    else break;
  }
  if (s || ll<cap || ll>(cap ? LONG_MAX : LLONG_MAX)) error_exit("bad %s", arg);
  free(arg);

  return ll;
}

void dd_main()
{
  char **args, *arg;
  unsigned long long bs = 0, count = ULLONG_MAX, seek = 0, skip = 0;
  int trunc = O_TRUNC;

  TT.show_xfer = TT.show_records = 1;

  TT.in.sz = TT.out.sz = 512; //default io block size
  for (args = toys.optargs; (arg = *args); args++) {
    if (strstart(&arg, "bs=")) bs = argxarg(arg, 1);
    else if (strstart(&arg, "ibs=")) TT.in.sz = argxarg(arg, 1);
    else if (strstart(&arg, "obs=")) TT.out.sz = argxarg(arg, 1);
    else if (strstart(&arg, "count=")) count = argxarg(arg, 0);
    else if (strstart(&arg, "if=")) TT.in.name = arg;
    else if (strstart(&arg, "of=")) TT.out.name = arg;
    else if (strstart(&arg, "seek=")) seek = argxarg(arg, 0);
    else if (strstart(&arg, "skip=")) skip = argxarg(arg, 0);
    else if (strstart(&arg, "status=")) {
      if (!strcmp(arg, "noxfer")) TT.show_xfer = 0;
      else if (!strcmp(arg, "none")) TT.show_xfer = TT.show_records = 0;
      else error_exit("unknown status '%s'", arg);
    } else if (strstart(&arg, "conv="))
      parse_flags("conv", arg, dd_conv, ARRAY_LEN(dd_conv), &TT.conv);
    else if (strstart(&arg, "iflag="))
      parse_flags("iflag", arg, dd_iflag, ARRAY_LEN(dd_iflag), &TT.iflag);
    else if (strstart(&arg, "oflag="))
      parse_flags("oflag", arg, dd_oflag, ARRAY_LEN(dd_oflag), &TT.oflag);
    else error_exit("bad arg %s", arg);
  }
  if (bs) TT.in.sz = TT.out.sz = bs;

  sigatexit(status);
  xsignal(SIGUSR1, status);
  TT.start = millitime();

  // For bs=, in/out is done as it is. so only in.sz is enough.
  // With Single buffer there will be overflow in a read following partial read.
  TT.in.bp = TT.out.bp = TT.buff = xmalloc(TT.in.sz+TT.out.sz*!bs);

  if (!TT.in.name) TT.in.name = "stdin";
  else TT.in.fd = xopenro(TT.in.name);

  if (TT.conv & _DD_conv_notrunc) trunc = 0;

  if (!TT.out.name) {
    TT.out.name = "stdout";
    TT.out.fd = 1;
  } else TT.out.fd = xcreate(TT.out.name,
    O_WRONLY|O_CREAT|(trunc*!seek), 0666);

  // Implement skip=
  if (skip) {
    if (!(TT.iflag & _DD_iflag_skip_bytes)) skip *= TT.in.sz;
    if (lseek(TT.in.fd, skip, SEEK_CUR) < 0) {
      while (skip > 0) {
        ssize_t n = read(TT.in.fd, TT.in.bp, minof(skip, TT.in.sz));

        if (n < 0) {
          perror_msg("%s", TT.in.name);
          if (TT.conv & _DD_conv_noerror) status();
          else return;
        } else if (!n) {
          xprintf("%s: Can't skip\n", TT.in.name);
          return;
        }
        skip -= n;
      }
    }
  }

  // Implement seek= and truncate as necessary. We handled position zero
  // truncate with O_TRUNC on open, so output to /dev/null etc doesn't error.
  if (!(TT.oflag & _DD_oflag_seek_bytes)) seek *= TT.out.sz;
  if (seek) {
    struct stat st;

    xlseek(TT.out.fd, seek, SEEK_CUR);
    if (trunc && !fstat(TT.out.fd, &st) && S_ISREG(st.st_mode)
      && ftruncate(TT.out.fd, seek)) perror_exit("truncate");
  }

  if (count!=ULLONG_MAX && !(TT.iflag & _DD_iflag_count_bytes))
    count = overmul(count, TT.in.sz);

  while (count) {
    ssize_t n;

    TT.in.bp = TT.buff + TT.in.count;
    if (TT.conv & _DD_conv_sync) memset(TT.in.bp, 0, TT.in.sz);
    errno = 0;
    if (1>(n = read(TT.in.fd, TT.in.bp, minof(count, TT.in.sz)))) {
      if (errno == EINTR) continue;
      if (!n) break;

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
    count -= n;

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
  if ((TT.conv & _DD_conv_fsync) && fsync(TT.out.fd))
    perror_exit("%s: fsync", TT.out.name);

  if (CFG_TOYBOX_FREE) {
    close(TT.in.fd);
    close(TT.out.fd);
    if (TT.buff) free(TT.buff);
  }
}
