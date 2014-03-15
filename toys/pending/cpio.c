/* cpio.c - a basic cpio
 *
 * Written 2013 AD by Isaac Dunham; this code is placed under the 
 * same license as toybox or as CC0, at your option.
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cpio.html
 *
 * http://pubs.opengroup.org/onlinepubs/7908799/xcu/cpio.html
 * (Yes, that's SUSv2, the newer standards removed it around the time RPM
 * and initramfs started heavily using this archive format. Go figure.)

USE_CPIO(NEWTOY(cpio, "H:di|o|t|uF:[!iot][!dot][!uot]", TOYFLAG_BIN))

config CPIO
  bool "cpio"
  default n
  help
    usage: cpio {-o|-t|-i[du]} [-H FMT] [-F ARCHIVE]

    copy files into and out of a "newc" format cpio archive

    Actions:
    -o	create archive (stdin is a list of files, stdout is an archive)
    -t	list files (stdin is an archive, stdout is a list of files)
    -i	extract from archive into file system (stdin is an archive)

    Extract options:
    -d	create leading directories when extracting an archive
    -u	always overwrite files (default)

    Other options:
    -H FMT	archive format (ignored, only newc supported)
    -F ARCHIVE	read from or write to ARCHIVE file
*/

#define FOR_cpio
#include "toys.h"

GLOBALS(
  char *archive;
  char *fmt;

  int outfd;
)

// 110 bytes
struct newc_header {
  char    c_magic[6];
  char    c_ino[8];
  char    c_mode[8];
  char    c_uid[8];
  char    c_gid[8];
  char    c_nlink[8];
  char    c_mtime[8];
  char    c_filesize[8];
  char    c_devmajor[8];
  char    c_devminor[8];
  char    c_rdevmajor[8];
  char    c_rdevminor[8];
  char    c_namesize[8];
  char    c_check[8];
};

void write_cpio_member(int fd, char *name, struct stat buf)
{
  unsigned nlen = strlen(name)+1, error = 0, zero = 0;
  ssize_t llen;

  if (!S_ISREG(buf.st_mode) && !S_ISLNK(buf.st_mode)) buf.st_size = 0;
  else if (buf.st_size >> 32) {
    perror_msg("skipping >2G file '%s'", name);
    return;
  }

  llen = sprintf(toybuf,
    "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
    (int)(buf.st_ino), buf.st_mode, buf.st_uid, buf.st_gid, (int)buf.st_nlink,
    (int)(buf.st_mtime), (int)(buf.st_size), major(buf.st_dev),
    minor(buf.st_dev), major(buf.st_rdev), minor(buf.st_rdev), nlen, 0);
  xwrite(TT.outfd, toybuf, llen);
  xwrite(TT.outfd, name, nlen);

  // NUL Pad header up to 4 multiple bytes.
  llen = (llen + nlen) & 3;
  if (llen) xwrite(TT.outfd, &zero, 4-llen); 

  // Write out body for symlink or regular file
  llen = buf.st_size;
  if (S_ISLNK(buf.st_mode)) {
    if (readlink(name, toybuf, sizeof(toybuf)-1) == llen)
      xwrite(TT.outfd, toybuf, llen);
    else perror_msg("readlink '%s'", name);
  } else while (llen) {
    nlen = llen > sizeof(toybuf) ? sizeof(toybuf) : llen;
    // If read fails, write anyway (we already wrote size in the header).
    if (nlen != readall(fd, toybuf, nlen))
      if (!error++) perror_msg("bad read from file '%s'", name);
    xwrite(TT.outfd, toybuf, nlen);
  }
  llen = buf.st_size & 3;
  if (nlen) write(TT.outfd, &zero, 4-llen);
}

// Iterate through a list of files read from stdin. No users need rw.
void loopfiles_stdin(void)
{
  char *name = 0;
  size_t size = 0;

  for (;;) {
    struct stat st;
    int len, fd;

    len = getline(&name, &size, stdin);
    if (!name) break;
    if (name[len-1] == '\n') name[--len] = 0;
    if (lstat(name, &st) || (fd = open(name, O_RDONLY))<0)
      perror_msg("%s", name);
    else {
      write_cpio_member(fd, name, st);
      close(fd);
    }
  }
  free(name);
}

//convert hex to uint; mostly to allow using bits of non-terminated strings
unsigned int htou(char * hex)
{
  unsigned int ret = 0, i = 0;

  for (;(i < 8 && hex[i]);) {
     ret *= 16;
     switch(hex[i]) { 
     case '0':
       break;
     case '1': 
     case '2': 
     case '3': 
     case '4': 
     case '5': 
     case '6': 
     case '7': 
     case '8': 
     case '9': 
       ret += hex[i] - '1' + 1;
       break;
     case 'A': 
     case 'B': 
     case 'C': 
     case 'D': 
     case 'E': 
     case 'F': 
       ret += hex[i] - 'A' + 10;
       break;
     }
     i++;
  }
  return ret;
}

// Read one cpio record. Returns 0 for last record, 1 for "continue".
int read_cpio_member(int fd, int how)
{
  uint32_t nsize, fsize;
  mode_t mode = 0;
  int pad, ofd = 0; 
  struct newc_header hdr;
  char *name, *lastdir;
  dev_t dev = 0;

  xreadall(fd, &hdr, sizeof(struct newc_header));
  nsize = htou(hdr.c_namesize);
  xreadall(fd, name = xmalloc(nsize), nsize);
  if (!strcmp("TRAILER!!!", name)) return 0;
  fsize = htou(hdr.c_filesize);
  mode += htou(hdr.c_mode);
  pad = 4 - ((nsize + 2) % 4); // 2 == sizeof(struct newc_header) % 4
  if (pad < 4) xreadall(fd, toybuf, pad);
  pad = 4 - (fsize % 4);

  if ((toys.optflags&FLAG_d) && (lastdir = strrchr(name, '/')))
    if (mkpathat(AT_FDCWD, name, 0, 2)) perror_msg("mkpath '%s'", name);

  if (how & 1) {
    if (S_ISDIR(mode)) ofd = mkdir(name, mode);
    else if (S_ISLNK(mode)) {
      memset(toybuf, 0, sizeof(toybuf));
      if (fsize < sizeof(toybuf)) {
        pad = readall(fd, toybuf, fsize); 
	if (pad < fsize) error_exit("short archive");
	pad = 4 - (fsize % 4);
	fsize = 0;
        if (symlink(toybuf, name)) {
	  perror_msg("could not write link %s", name);
	  toys.exitval |= 1;
	}
      } else {
        perror_msg("link too long: %s", name);
	toys.exitval |= 1;
      }
    } else if (S_ISBLK(mode)||S_ISCHR(mode)||S_ISFIFO(mode)||S_ISSOCK(mode)) {
      dev = makedev(htou(hdr.c_rdevmajor),htou(hdr.c_rdevminor));
      ofd = mknod(name, mode, dev);
    } else ofd = creat(name, mode);
    if (ofd == -1) {
      error_msg("could not create %s", name);
      toys.exitval |= 1;
    }
  }
  errno = 0;
  if (how & 2) puts(name);
  while (fsize) {
    int i;
    memset(toybuf, 0, sizeof(toybuf));
    i = readall(fd, toybuf, (fsize>sizeof(toybuf)) ? sizeof(toybuf) : fsize);
    if (i < 1) error_exit("archive too short");
    if (ofd > 0) writeall(ofd, toybuf, i);
    fsize -= i;
  }
  if (pad < 4) xreadall(fd, toybuf, pad);
  return 1;
}

void read_cpio_archive(int fd, int how)
{
  for(;;) if (!read_cpio_member(fd, how)) return;
}

void cpio_main(void)
{
  TT.outfd = 1;

  if (TT.archive) {
    if (toys.optflags & (FLAG_i|FLAG_t)) {
      xclose(0);
      xopen(TT.archive, O_RDONLY);
    } else if (toys.optflags & FLAG_o) {
      xclose(1);
      xcreate(TT.archive, O_CREAT|O_WRONLY|O_TRUNC, 0644);
    }
  }

  if (toys.optflags & FLAG_t) read_cpio_archive(0, 2);
  else if (toys.optflags & FLAG_i) read_cpio_archive(0, 1);
  else if (toys.optflags & FLAG_o) {
      loopfiles_stdin();
      write(1, "07070100000000000000000000000000000000000000010000000000000000"
      "000000000000000000000000000000000000000B00000000TRAILER!!!\0\0\0", 124);
  } else error_exit("must use one of -iot");
}
