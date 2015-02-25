/* stat.c : display file or file system status
 * Copyright 2012 <warior.linux@gmail.com>
 * Copyright 2013 <anand.sinha85@gmail.com>

USE_STAT(NEWTOY(stat, "c:f", TOYFLAG_BIN)) 

config STAT
  bool stat
  default y
  help
    usage: stat [-f] [-c FORMAT] FILE...

    Display status of files or filesystems.

    -f display filesystem status instead of file status
    -c Output specified FORMAT string instead of default

    The valid format escape sequences for files:
    %a  Access bits (octal) |%A  Access bits (flags)|%b  Blocks allocated
    %B  Bytes per block     |%d  Device ID (dec)    |%D  Device ID (hex)
    %f  All mode bits (hex) |%F  File type          |%g  Group ID
    %G  Group name          |%h  Hard links         |%i  Inode
    %n  Filename            |%N  Long filename      |%o  I/O block size
    %s  Size (bytes)        |%u  User ID            |%U  User name
    %x  Access time         |%X  Access unix time   |%y  File write time
    %Y  File write unix time|%z  Dir change time    |%Z  Dir change unix time

    The valid format escape sequences for filesystems:
    %a  Available blocks    |%b  Total blocks       |%c  Total inodes
    %d  Free inodes         |%f  Free blocks        |%i  File system ID
    %l  Max filename length |%n  File name          |%s  Fragment size
    %S  Best transfer size  |%t  File system type
*/

#define FOR_stat
#include "toys.h"

GLOBALS(
  char *fmt;

  union {
    struct stat st;
    struct statfs sf;
  } stat;
  struct passwd *user_name;
  struct group *group_name;
)


// Note: the atime, mtime, and ctime fields in struct stat are the start
// of embedded struct timespec, but posix won't let them use that
// struct definition for legacy/namespace reasons.

static void date_stat_format(struct timespec *ts)
{
  strftime(toybuf, sizeof(toybuf), "%Y-%m-%d %H:%M:%S",
    localtime(&(ts->tv_sec)));
  xprintf("%s.%09d", toybuf, ts->tv_nsec);
}

static void print_stat(char type)
{
  struct stat *stat = (struct stat *)&TT.stat;

  if (type == 'a') xprintf("%lo", stat->st_mode & ~S_IFMT);
  else if (type == 'A') {
    char str[11];

    mode_to_string(stat->st_mode, str);
    xprintf("%s", str);
  } else if (type == 'b') xprintf("%llu", stat->st_blocks);
  else if (type == 'B') xprintf("%lu", stat->st_blksize);
  else if (type == 'd') xprintf("%ldd", stat->st_dev);
  else if (type == 'D') xprintf("%llxh", stat->st_dev);
  else if (type == 'f') xprintf("%lx", stat->st_mode);
  else if (type == 'F') {
    char *t = "character device\0directory\0block device\0" \
              "regular file\0symbolic link\0socket\0FIFO (named pipe)";
    int i, filetype = stat->st_mode & S_IFMT;

    for (i = 1; filetype != (i*8192) && i < 7; i++) t += strlen(t)+1;
    if (!stat->st_size && filetype == S_IFREG) t = "regular empty file";
    xprintf("%s", t);
  } else if (type == 'g') xprintf("%lu", stat->st_gid);
  else if (type == 'G') xprintf("%8s", TT.user_name->pw_name);
  else if (type == 'h') xprintf("%lu", stat->st_nlink);
  else if (type == 'i') xprintf("%llu", stat->st_ino);
  else if (type == 'N') {
    xprintf("`%s'", *toys.optargs);
    if (S_ISLNK(stat->st_mode))
      if (0<readlink(*toys.optargs, toybuf, sizeof(toybuf)))
        xprintf(" -> `%s'", toybuf);
  } else if (type == 'o') xprintf("%lu", stat->st_blksize);
  else if (type == 's') xprintf("%llu", stat->st_size);
  else if (type == 'u') xprintf("%lu", stat->st_uid);
  else if (type == 'U') xprintf("%8s", TT.user_name->pw_name);
  else if (type == 'x') date_stat_format((void *)&stat->st_atime);
  else if (type == 'X') xprintf("%llu", (long long)stat->st_atime);
  else if (type == 'y') date_stat_format((void *)&stat->st_mtime);
  else if (type == 'Y') xprintf("%llu", (long long)stat->st_mtime);
  else if (type == 'z') date_stat_format((void *)&stat->st_ctime);
  else if (type == 'Z') xprintf("%llu", (long long)stat->st_ctime);
  else xprintf("?");
}

static void print_statfs(char type) {
  struct statfs *statfs = (struct statfs *)&TT.stat;

  if (type == 'a') xprintf("%llu", statfs->f_bavail);
  else if (type == 'b') xprintf("%llu", statfs->f_blocks);
  else if (type == 'c') xprintf("%llu", statfs->f_files);
  else if (type == 'd') xprintf("%llu", statfs->f_ffree);
  else if (type == 'f') xprintf("%llu", statfs->f_bfree);
  else if (type == 'l') xprintf("%ld", statfs->f_namelen);
  else if (type == 't') xprintf("%lx", statfs->f_type);
  else if (type == 'i')
    xprintf("%08x%08x", statfs->f_fsid.__val[0], statfs->f_fsid.__val[1]);
  else if (type == 's') xprintf("%d", statfs->f_frsize);
  else if (type == 'S') xprintf("%d", statfs->f_bsize);
  else xprintf("?");
}

void stat_main(void)
{
  int flagf = toys.optflags & FLAG_f;
  char *format = flagf
    ? "  File: \"%n\"\n    ID: %i Namelen: %l    Type: %t\n"
      "Block Size: %s    Fundamental block size: %S\n"
      "Blocks: Total: %b\tFree: %f\tAvailable: %a\n"
      "Inodes: Total: %c\tFree: %d"
    : "  File: %N\n  Size: %s\t Blocks: %b\t IO Blocks: %B\t%F\n"
      "Device: %D\t Inode: %i\t Links: %h\n"
      "Access: (%a/%A)\tUid: (%u/%U)\tGid: (%g/%G)\n"
      "Access: %x\nModify: %y\nChange: %z";

  if (toys.optflags & FLAG_c) format = TT.fmt;

  for (; *toys.optargs; toys.optargs++) {
    char *f;

    if (flagf && !statfs(*toys.optargs, (void *)&TT.stat));
    else if (!flagf && !lstat(*toys.optargs, (void *)&TT.stat)) {
      struct stat *stat = (struct stat*)&TT.stat;

      // check user and group name
      TT.user_name = getpwuid(stat->st_uid);
      TT.group_name = getgrgid(stat->st_gid);
    } else {
      perror_msg("'%s'", *toys.optargs);
      continue;
    }

    for (f = format; *f; f++) {
      if (*f != '%') putchar(*f);
      else {
        if (*++f == 'n') xprintf("%s", *toys.optargs);
        else if (flagf) print_statfs(*f);
        else print_stat(*f);
      }
    }
    xputc('\n');
  }
}
