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

USE_CPIO(NEWTOY(cpio, "H:diotuF:", TOYFLAG_BIN))

config CPIO
  bool "cpio"
  default n
  help
    usage: cpio { -i[du] | -o | -t } [-H FMT] [-F ARCHIVE]

    copy files into and out of an archive
    -d  create leading directories when extracting an archive
    -i  extract from archive into file system (stdin is an archive)
    -o  create archive (stdin is a list of files, stdout is an archive)
    -t  list files (stdin is an archive, stdout is a list of files)
    -u  always overwrite files (default)
    -H FMT   write archive in specified format:
    newc  SVR4 new character format (default)
    -F ARCHIVE  read from or write to ARCHIVE
*/

#define FOR_cpio
#include "toys.h"

GLOBALS(
  char *archive;
  char *fmt;
)

/* Iterate through a list of files, read from stdin.
 * No users need rw.
 */
void loopfiles_stdin(void (*function)(int fd, char *name, struct stat st))
{
  int fd;
  struct stat st;
  char *name = toybuf;

  while (name) {
    memset(toybuf, 0, sizeof(toybuf));
    name = fgets(toybuf, sizeof(toybuf) - 1, stdin);
    
    if (name) {
      if (toybuf[strlen(name) - 1] == '\n' ) { 
        toybuf[strlen(name) - 1 ] = '\0';
        if (lstat(name, &st) == -1) verror_msg(name, errno, NULL);
	if (errno) continue;
	fd = open(name, O_RDONLY);
	if (fd > 0 || !S_ISREG(st.st_mode)) {
          function(fd, name, st);
	  close(fd);
	}
	errno = 0;
      }
    }
  }
}

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
  char ahdr[sizeof(struct newc_header) + 1];
  struct newc_header *hdr = (struct newc_header *)ahdr;
  size_t out = 0;
  unsigned int n = 0x00000000, nlen = strlen(name) + 1;

  memset(hdr, '0', sizeof(struct newc_header));
  if (S_ISDIR(buf.st_mode) || S_ISBLK(buf.st_mode) || S_ISCHR(buf.st_mode)
     || S_ISFIFO(buf.st_mode) || S_ISSOCK(buf.st_mode)) 
    buf.st_size = 0;
  snprintf((char *)(hdr), sizeof(struct newc_header)+1, 
          "070701%08X%08X" "%08X%08X"
	  "%08X%08X%08X"
	   "%08X%08X" "%08X%08X%08X00000000",
	   (unsigned int)(buf.st_ino), buf.st_mode, buf.st_uid, buf.st_gid, 
	   buf.st_nlink, (uint32_t)(buf.st_mtime), (uint32_t)(buf.st_size), 
	   major(buf.st_dev), minor(buf.st_dev),
           major(buf.st_rdev), minor(buf.st_rdev), nlen);
  write(1, hdr, sizeof(struct newc_header));
  write(1, name, nlen);
  if ((nlen + 2) & 3) write(1, &n, 4 - ((nlen + 2) & 3)); 
  if (S_ISLNK(buf.st_mode)) {
    ssize_t llen = readlink(name, toybuf, sizeof(toybuf) - 1);
    if (llen > 0) {
      toybuf[llen] = '\0';
      write(1, toybuf, buf.st_size);
    }
  } else if (buf.st_size) {
    for (; (lseek(fd, 0, SEEK_CUR) < (uint32_t)(buf.st_size));) {
      out = read(fd, toybuf, sizeof(toybuf));
      if (out > 0) write(1, toybuf, out);
      if (errno || out < sizeof(toybuf)) break;
    }
  }
  if (buf.st_size & 3) write(1, &n, 4 - (buf.st_size & 3));
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
  if (TT.archive) {
    if (toys.optflags & (FLAG_i|FLAG_t)) {
      xclose(0);
      xopen(TT.archive, O_RDONLY);
    } else if (toys.optflags & FLAG_o) {
      xclose(1);
      xcreate(TT.archive, O_CREAT|O_WRONLY|O_TRUNC, 0755);
    }
  }

  if (toys.optflags & FLAG_t) read_cpio_archive(0, 2);
  else if (toys.optflags & FLAG_i) read_cpio_archive(0, 1);
  else if (toys.optflags & FLAG_o) {
      loopfiles_stdin(write_cpio_member);
      write(1, "07070100000000000000000000000000000000000000010000000000000000"
      "000000000000000000000000000000000000000B00000000TRAILER!!!\0\0\0", 124);
  } else error_exit("must use one of -iot");
}
