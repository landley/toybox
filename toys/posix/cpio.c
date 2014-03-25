/* cpio.c - a basic cpio
 *
 * Written 2013 AD by Isaac Dunham; this code is placed under the 
 * same license as toybox or as CC0, at your option.
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cpio.html
 * and http://pubs.opengroup.org/onlinepubs/7908799/xcu/cpio.html
 *
 * Yes, that's SUSv2, the newer standards removed it around the time RPM
 * and initramfs started heavily using this archive format.
 *
 * Modern cpio expanded header to 110 bytes (first field 6 bytes, rest are 8).
 * In order: magic ino mode uid gid nlink mtime filesize devmajor devminor
 * rdevmajor rdevminor namesize check

USE_CPIO(NEWTOY(cpio, "duH:i|t|F:o|v(verbose)[!io][!ot]", TOYFLAG_BIN))

config CPIO
  bool "cpio"
  default y
  help
    usage: cpio -{o|t|i} [-v] [--verbose] [-F FILE] [ignored: -du -H newc]

    copy files into and out of a "newc" format cpio archive

    -F FILE	use archive FILE instead of stdin/stdout
    -i	extract from archive into file system (stdin=archive)
    -o	create archive (stdin=list of files, stdout=archive)
    -t	test files (list only, stdin=archive, stdout=list of files)
    -v	verbose (list files during create/extract)
*/

#define FOR_cpio
#include "toys.h"

GLOBALS(
  char *archive;
  char *fmt;
)

// Read strings, tail padded to 4 byte alignment. Argument "align" is amount
// by which start of string isn't aligned (usually 0).
static char *strpad(int fd, unsigned len, unsigned align)
{
  char *str;

  align = (align + len) & 3;
  if (align) len += (4-align);

  xreadall(fd, str = xmalloc(len+1), len);
  str[len]=0; // redundant, in case archive is bad

  return str;
}

//convert hex to uint; mostly to allow using bits of non-terminated strings
unsigned x8u(char *hex)
{
  unsigned val, inpos = 8, outpos;
  char pattern[6];

  while (*hex == '0') {
    hex++;
    if (!--inpos) return 0;
  }
  // Because scanf gratuitously treats %*X differently than printf does.
  sprintf(pattern, "%%%dX%%n", inpos);
  sscanf(hex, pattern, &val, &outpos);
  if (inpos != outpos) error_exit("bad header");

  return val;
}

void cpio_main(void)
{
  int afd;

  // Subtle bit: FLAG_o is 1 so we can just use it to select stdin/stdout.

  afd = toys.optflags & FLAG_o;
  if (TT.archive) {
    int perm = (toys.optflags & FLAG_o) ? O_CREAT|O_WRONLY|O_TRUNC : O_RDONLY;

    afd = xcreate(TT.archive, perm, 0644);
  }

  // read cpio archive

  if (toys.optflags & (FLAG_i|FLAG_t)) for (;;) {
    char *name, *tofree, *data;
    unsigned size, mode;
    int test = toys.optflags & FLAG_t, err = 0;

    // Read header and name.
    xreadall(afd, toybuf, 110);
    tofree = name = strpad(afd, x8u(toybuf+94), 110);
    if (!strcmp("TRAILER!!!", name)) break;

    // If you want to extract absolute paths, "cd /" and run cpio.
    while (*name == '/') name++;

    // Align to 4 bytes. Note header is 110 bytes which is 2 bytes over.

    size = x8u(toybuf+54);
    mode = x8u(toybuf+14);

    if (toys.optflags & (FLAG_t|FLAG_v)) puts(name);

    if (!test && strrchr(name, '/') && mkpathat(AT_FDCWD, name, 0, 2)) {
      perror_msg("mkpath '%s'", name);
      test++;
    }

    // Consume entire record even if it couldn't create file, so we're
    // properly aligned with next file.

    if (S_ISDIR(mode)) {
      if (!test) err = mkdir(name, mode);
    } else if (S_ISLNK(mode)) {
      data = strpad(afd, size, 0);
      if (!test) err = symlink(data, name);
    } else if (S_ISREG(mode)) {
      int fd;

      // If write fails, we still need to read/discard data to continue with
      // archive. Since doing so overwrites errno, report error now
      fd = test ? 0 : open(name, O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW, mode);
      if (fd < 0) {
        perror_msg("create %s", name);
        test++;
      }

      data = toybuf;
      while (size) {
        if (size < sizeof(toybuf)) data = strpad(afd, size, 0);
        else xreadall(afd, toybuf, sizeof(toybuf));
        if (!test) xwrite(fd, data, data == toybuf ? sizeof(toybuf) : size);
        if (data != toybuf) {
          free(data);
          break;
        }
        size -= sizeof(toybuf);
      }
      close(fd);
    } else if (!test)
      err = mknod(name, mode, makedev(x8u(toybuf+62), x8u(toybuf+70)));

    if (err<0) perror_msg("create '%s'", name);
    free(tofree);

  // Output cpio archive

  } else {
    char *name = 0;
    size_t size = 0;

    for (;;) {
      struct stat st;
      unsigned nlen = strlen(name)+1, error = 0, zero = 0;
      int len, fd = -1;
      ssize_t llen;

      len = getline(&name, &size, stdin);
      if (len<1) break;
      if (name[len-1] == '\n') name[--len] = 0;
      if (lstat(name, &st)
          || (S_ISREG(st.st_mode) && (fd = open(name, O_RDONLY))<0))
      {
        perror_msg("%s", name);
        continue;
      }

      if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) st.st_size = 0;
      if (st.st_size >> 32) perror_msg("skipping >2G file '%s'", name);
      else {
        llen = sprintf(toybuf,
          "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
          (int)st.st_ino, st.st_mode, st.st_uid, st.st_gid, (int)st.st_nlink,
          (int)st.st_mtime, (int)st.st_size, major(st.st_dev),
          minor(st.st_dev), major(st.st_rdev), minor(st.st_rdev), nlen, 0);
        xwrite(afd, toybuf, llen);
        xwrite(afd, name, nlen);

        // NUL Pad header up to 4 multiple bytes.
        llen = (llen + nlen) & 3;
        if (llen) xwrite(afd, &zero, 4-llen); 

        // Write out body for symlink or regular file
        llen = st.st_size;
        if (S_ISLNK(st.st_mode)) {
          if (readlink(name, toybuf, sizeof(toybuf)-1) == llen)
            xwrite(afd, toybuf, llen);
          else perror_msg("readlink '%s'", name);
        } else while (llen) {
          nlen = llen > sizeof(toybuf) ? sizeof(toybuf) : llen;
          llen -= nlen;
          // If read fails, write anyway (already wrote size in header)
          if (nlen != readall(fd, toybuf, nlen))
            if (!error++) perror_msg("bad read from file '%s'", name);
          xwrite(afd, toybuf, nlen);
        }
        llen = st.st_size & 3;
        if (llen) write(afd, &zero, 4-llen);
      }
      close(fd);
    }
    free(name);

    xwrite(afd, toybuf,
      sprintf(toybuf, "070701%040X%056X%08XTRAILER!!!%c%c%c",
              1, 0x0b, 0, 0, 0, 0));
  }
}
