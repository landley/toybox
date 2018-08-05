/* gzip.c - gzip/gunzip/zcat
 *
 * Copyright 2017 The Android Open Source Project
 *
 * GZIP RFC: http://www.ietf.org/rfc/rfc1952.txt
 *
 * todo: qtv --rsyncable

// gzip.net version allows all options for all commands.
USE_GZIP(NEWTOY(gzip,     "cdfk123456789[-123456789]", TOYFLAG_USR|TOYFLAG_BIN))
USE_GUNZIP(NEWTOY(gunzip, "cdfk123456789[-123456789]", TOYFLAG_USR|TOYFLAG_BIN))
USE_ZCAT(NEWTOY(zcat,     "cdfk123456789[-123456789]", TOYFLAG_USR|TOYFLAG_BIN))

config GZIP
  bool "gzip"
  default y
  help
    usage: gzip [-19cdfk] [FILE...]

    Compress files. With no files, compresses stdin to stdout.
    On success, the input files are removed and replaced by new
    files with the .gz suffix.

    -c	Output to stdout
    -d	Decompress (act as gunzip)
    -f	Force: allow overwrite of output file
    -k	Keep input files (default is to remove)
    -#	Compression level 1-9 (1:fastest, 6:default, 9:best)

config GUNZIP
  bool "gunzip"
  default y
  help
    usage: gunzip [-cfk] [FILE...]

    Decompress files. With no files, decompresses stdin to stdout.
    On success, the input files are removed and replaced by new
    files without the .gz suffix.

    -c	Output to stdout (act as zcat)
    -f	Force: allow read from tty
    -k	Keep input files (default is to remove)

config ZCAT
  bool "zcat"
  default y
  help
    usage: zcat [FILE...]

    Decompress files to stdout. Like `gzip -dc`.

    -f	Force: allow read from tty
*/

#define FORCE_FLAGS
#define FOR_gzip
#include "toys.h"

GLOBALS(
  int level;
)

// Use assembly optimized zlib code?
#if CFG_TOYBOX_LIBZ
#include <zlib.h>

// Read fron in_fd, write to out_fd, decompress if dd else compress
static int do_deflate(int in_fd, int out_fd, int dd, int level)
{
  int len, err = 0;
  char *b = "r";
  gzFile gz;

  if (!dd) {
    sprintf(b = toybuf, "w%d", level);
    if (out_fd == 1) out_fd = xdup(out_fd);
  }
  if (!(gz = gzdopen(dd ? in_fd : out_fd, b))) perror_exit("gzdopen");
  if (dd) {
    while ((len = gzread(gz, toybuf, sizeof(toybuf))) > 0)
      if (len != writeall(out_fd, toybuf, len)) break;
  } else {
    while ((len = read(in_fd, toybuf, sizeof(toybuf))) > 0)
      if (len != gzwrite(gz, toybuf, len))  break;
  }

  err = !!len;
  if (len>0 || err == Z_ERRNO) perror_msg(dd ? "write" : "read");
  if (len<0)
    error_msg("%s%s: %s", "gz", dd ? "read" : "write", gzerror(gz, &len));

  if (gzclose(gz) != Z_OK) perror_msg("gzclose"), err++;

  return err;
}

// Use toybox's builtin lib/deflate.c
#else

// Read from in_fd, write to out_fd, decompress if dd else compress
static int do_deflate(int in_fd, int out_fd, int dd, int level)
{
  int x;

  if (dd) WOULD_EXIT(x, gunzip_fd(in_fd, out_fd));
  else WOULD_EXIT(x, gzip_fd(in_fd, out_fd));

  return x;
}

#endif

static void do_gzip(int in_fd, char *arg)
{
  struct stat sb;
  int len, out_fd = 0;
  char *out_name = 0;

  // Are we writing to stdout?
  if (!in_fd || (toys.optflags&FLAG_c)) out_fd = 1;
  if (isatty(in_fd)) {
    if (!(toys.optflags&FLAG_f))
      return error_msg("%s:need -f to read TTY"+3*!!in_fd, arg);
    else out_fd = 1;
  }

  // Are we reading file.gz to write to file?
  if (!out_fd) {
    if (fstat(in_fd, &sb)) return perror_msg("%s", arg);

    if (!(toys.optflags&FLAG_d)) out_name = xmprintf("%s%s", arg, ".gz");
    else {
      // "gunzip x.gz" will decompress "x.gz" to "x".
      if ((len = strlen(arg))<4 || strcmp(arg+len-3, ".gz"))
        return error_msg("no .gz: %s", arg);
      out_name = xstrdup(arg);
      out_name[len-3] = 0;
    }

    out_fd = xcreate(out_name,
      O_CREAT|O_WRONLY|WARN_ONLY|(O_EXCL*!(toys.optflags&FLAG_f)), sb.st_mode);
    if (out_fd == -1) return;
  }

  if (do_deflate(in_fd, out_fd, toys.optflags&FLAG_d, TT.level) && out_name)
    arg = out_name;
  if (out_fd != 1) close(out_fd);

  if (out_name) {
    struct timespec times[] = { sb.st_atim, sb.st_mtim };

    if (utimensat(AT_FDCWD, out_name, times, 0)) perror_exit("utimensat");
    if (!(toys.optflags&FLAG_k)) if (unlink(arg)) perror_msg("unlink %s", arg);
    free(out_name);
  }
}

void gzip_main(void)
{
  // This depends on 1-9 being at the end of the option list
  for (TT.level = 0; TT.level<9; TT.level++)
    if ((toys.optflags>>TT.level)&1) break;
  if (!(TT.level = 9-TT.level)) TT.level = 6;

  loopfiles(toys.optargs, do_gzip);
}

void gunzip_main(void)
{
  toys.optflags |= FLAG_d;
  gzip_main();
}

void zcat_main(void)
{
  toys.optflags |= (FLAG_c|FLAG_d);
  gzip_main();
}
