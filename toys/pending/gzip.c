/* gzip.c - gzip/gunzip/zcat
 *
 * Copyright 2017 The Android Open Source Project
 *
 * GZIP RFC: http://www.ietf.org/rfc/rfc1952.txt

// Existing implementations allow all options for all commands.
USE_GZIP(NEWTOY(gzip,     "cdfk123456789", TOYFLAG_USR|TOYFLAG_BIN))
USE_GUNZIP(NEWTOY(gunzip, "cdfk123456789", TOYFLAG_USR|TOYFLAG_BIN))
USE_ZCAT(NEWTOY(zcat,     "cdfk123456789", TOYFLAG_USR|TOYFLAG_BIN))

config GZIP
  bool "gzip"
  default y
  depends on TOYBOX_LIBZ
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
  depends on TOYBOX_LIBZ
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
  depends on TOYBOX_LIBZ
  help
    usage: zcat [FILE...]

    Decompress files to stdout. Like `gzip -dc`.

    -c	Output to stdout (default)
    -f	Force: allow read from tty
*/

#include <zlib.h>

#define FORCE_FLAGS
#define FOR_gzip
#include "toys.h"

GLOBALS(
  int level;
)

static void fix_time(const char *path, struct stat *sb)
{
  struct timespec times[] = { sb->st_atim, sb->st_mtim };

  if (utimensat(AT_FDCWD, path, times, 0)) perror_exit("utimensat");
}

static void gzerror_exit(gzFile f, char *what)
{
  int err;
  const char *msg = gzerror(f, &err);

  ((err == Z_ERRNO) ? perror_exit : error_exit)("%s: %s", what, msg);
}

static void do_gunzip(int in_fd, char *arg)
{
  struct stat sb;
  int len, both_files;
  char *in_name, *out_name;
  gzFile in;
  FILE *out;

  // "gunzip x.gz" will decompress "x.gz" to "x".
  len = strlen(arg);
  if (len > 3 && !strcmp(arg+len-3, ".gz")) {
    in_name = strdup(arg);
    out_name = strdup(arg);
    out_name[len-3] = '\0';
  } else if (!strcmp(arg, "-")) {
    // "-" means stdin; assume output to stdout.
    // TODO: require -f to read compressed data from tty?
    in_name = strdup("-");
    out_name = strdup("-");
  } else error_exit("unknown suffix: %s", arg);

  if (toys.optflags&FLAG_c) {
    free(out_name);
    out_name = strdup("-");
  }

  both_files = strcmp(in_name, "-") && strcmp(out_name, "-");
  if (both_files) xstat(in_name, &sb);

  in = gzdopen(in_fd, "r");
  if (in == NULL) perror_exit("gzdopen");
  if (!strcmp(out_name, "-")) out = stdout;
  else {
    int out_fd = xcreate(out_name,
      O_CREAT|O_WRONLY|((toys.optflags&FLAG_f)?0:O_EXCL),
      both_files?sb.st_mode:0666);

    out = xfdopen(out_fd, "w");
  }

  while ((len = gzread(in, toybuf, sizeof(toybuf))) > 0) {
    if (fwrite(toybuf, 1, len, out) != (size_t) len) perror_exit("writing");
  }
  if (len < 0) gzerror_exit(in, "gzread");
  if (out != stdout && fclose(out)) perror_exit("writing");
  if (gzclose(in) != Z_OK) error_exit("gzclose");

  if (both_files) fix_time(out_name, &sb);
  if (!(toys.optflags&(FLAG_c|FLAG_k))) unlink(in_name);
  free(in_name);
  free(out_name);
}

static void do_gzip(int in_fd, char *in_name)
{
  size_t len;
  char *out_name;
  FILE *in = xfdopen(in_fd, "r");
  gzFile out;
  struct stat sb;
  int both_files, out_fd;

  out_name = (toys.optflags&FLAG_c) ? strdup("-") : xmprintf("%s.gz", in_name);
  both_files = strcmp(in_name, "-") && strcmp(out_name, "-");
  if (both_files) xstat(in_name, &sb);

  if (!strcmp(out_name, "-")) out_fd = dup(1);
  else {
    out_fd = open(out_name, O_CREAT|O_WRONLY|((toys.optflags&FLAG_f)?0:O_EXCL),
      both_files?sb.st_mode:0);
  }
  if (out_fd == -1) perror_exit("open %s", out_name);

  snprintf(toybuf, sizeof(toybuf), "w%d", TT.level);
  out = gzdopen(out_fd, toybuf);
  if (out == NULL) perror_exit("gzdopen %s", out_name);

  while ((len = fread(toybuf, 1, sizeof(toybuf), in)) > 0) {
    if (gzwrite(out, toybuf, len) != (int) len) gzerror_exit(out, "gzwrite");
  }
  if (ferror(in)) perror_exit("fread");
  if (fclose(in)) perror_exit("fclose");
  if (gzclose(out) != Z_OK) error_exit("gzclose");

  if (both_files) fix_time(out_name, &sb);
  if (!(toys.optflags&(FLAG_c|FLAG_k))) unlink(in_name);
  free(out_name);
}

static void do_gz(int fd, char *name)
{
  if (toys.optflags&FLAG_d) do_gunzip(fd, name);
  else do_gzip(fd, name);
}

void gzip_main(void)
{
  int i = (toys.optflags&0x1ff);

  for (TT.level = (i == 0) ? 6 : 10; i; i >>= 1) --TT.level;

  // With no arguments, go from stdin to stdout.
  if (!*toys.optargs) toys.optflags |= FLAG_c;

  loopfiles(toys.optargs, do_gz);
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
