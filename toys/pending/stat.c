/* stat.c : display file or file system status
 * anand.sinha85@gmail.com
 * Copyright 2012 <warior.linux@gmail.com>

USE_STAT(NEWTOY(stat, "c:f", TOYFLAG_BIN)) 

config STAT
  bool stat
  default n
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

  void *stat;
  struct passwd *user_name;
  struct group *group_name;
  char *ftname, access_str[11];
)


static void date_stat_format(time_t time, int nano)
{
  static char buf[36];
  int len;

  len = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.", localtime(&time));
  sprintf(buf+len, "%09d", nano);
  xprintf("%s", buf);
}

static int print_stat(char type)
{
  struct stat *stat = (struct stat*)TT.stat;

  switch (type) {
    case 'a':
      xprintf("%04lo", stat->st_mode & ~S_IFMT);
      break;
    case 'A':
      xprintf("%s", TT.access_str);
      break;
    case 'b':
      xprintf("%llu", stat->st_blocks);
      break;
    case 'B':
      xprintf("%lu", stat->st_blksize);
      break;
    case 'd':
      xprintf("%ldd", stat->st_dev);
      break;
    case 'D':
      xprintf("%llxh", stat->st_dev);
      break;
    case 'f':
      xprintf("%lx", stat->st_mode);
      break;
    case 'F':
      xprintf("%s", TT.ftname);
      break;
    case 'g':
      xprintf("%lu", stat->st_gid);
      break;
    case 'G':
      xprintf("%8s", TT.user_name->pw_name);
      break;
    case 'h':
      xprintf("%lu", stat->st_nlink);
      break;
    case 'i':
      xprintf("%llu", stat->st_ino);
      break;
    case 'N':
      xprintf("`%s'", *toys.optargs);
      break;
    case 'o':
      xprintf("%lu", stat->st_blksize);
      break;
    case 's':
      xprintf("%llu", stat->st_size);
      break;
    case 'u':
      xprintf("%lu", stat->st_uid);
      break;
    case 'U':
      xprintf("%8s", TT.user_name->pw_name);
      break;
    case 'x':
      date_stat_format(stat->st_atime, stat->st_atim.tv_nsec);
      break;
    case 'X':
      xprintf("%llu", stat->st_atime);
      break;
    case 'y':
      date_stat_format(stat->st_mtime, stat->st_mtim.tv_nsec);
      break;
    case 'Y':
      xprintf("%llu", stat->st_mtime);
      break;
    case 'z':
      date_stat_format(stat->st_ctime, stat->st_ctim.tv_nsec);
      break;
    case 'Z':
      xprintf("%llu", stat->st_ctime);
      break;
    default:
      return 1;
  }
  return 0;
}

static int print_statfs(char type) {
  struct statfs *statfs = (struct statfs*)TT.stat;

  switch (type) {
    case 'a':
      xprintf("%lu", statfs->f_bavail);
      break;
    case 'b':
      xprintf("%lu", statfs->f_blocks);
      break;
    case 'c':
      xprintf("%lu", statfs->f_files);
      break;
    case 'd':
      xprintf("%lu", statfs->f_ffree);
      break;
    case 'f':
      xprintf("%lu", statfs->f_bfree);
      break;
    case 'i':
      xprintf("%x%x", statfs->f_fsid.__val[0], statfs->f_fsid.__val[1]);
      break;
    case 'l':
      xprintf("%ld", statfs->f_namelen);
      break;
    case 's':
      xprintf("%d", statfs->f_frsize);
      break;
    case 'S':
      xprintf("%d", statfs->f_bsize);
      break;
    case 't':
      xprintf("%lx", statfs->f_type);
      break;
    default:
      return 1;
  }
  return 0;
}

static int do_stat(char *path)
{
  struct stat *statf = (struct stat*)TT.stat;
  char *types = "character device\0directory\0block device\0" \
               "regular file\0symbolic link\0socket\0FIFO (named pipe)";
  int i, filetype;

  if (stat(path, statf) < 0) return 1;

  filetype = statf->st_mode & S_IFMT;
  TT.ftname = types;
  for (i = 1; filetype != (i*8192) && i < 7; i++)
    TT.ftname += strlen(TT.ftname)+1;
  if (!statf->st_size && filetype == S_IFREG)
    TT.ftname = "regular empty file";

  // check user and group name
  TT.user_name = getpwuid(statf->st_uid);
  TT.group_name = getgrgid(statf->st_gid);
  // function to get access in human readable format
  format_mode(&TT.access_str, statf->st_mode);

  return 0;
}

static int do_statfs(char *path)
{
  return statfs(path, TT.stat) < 0;
}

void stat_main(void)
{
  struct {
    char *fmt;
    int (*do_it)(char*);
    int (*print_it)(char);
    size_t size;
  } d, ds[2] = {
    {"  File: %N\n"
     "  Size: %s\t Blocks: %b\t IO Blocks: %B\t%F\n"
     "Device: %D\t Inode: %i\t Links: %h\n"
     "Access: (%a/%A)\tUid: (%u/%U)\tGid: (%g/%G)\n"
     "Access: %x\nModify: %y\nChange: %z",
     do_stat, print_stat, sizeof(struct stat)},
    {"  File: \"%n\"\n"
     "    ID: %i Namelen: %l    Type: %t\n"
     "Block Size: %s    Fundamental block size: %S\n"
     "Blocks: Total: %b\tFree: %f\tAvailable: %a\n"
     "Inodes: Total: %c\tFree: %d",
     do_statfs, print_statfs, sizeof(struct statfs)}
  };

  d = ds[toys.optflags & FLAG_f];
  TT.stat = xmalloc(d.size);
  if (toys.optflags & FLAG_c) d.fmt = TT.fmt;

  for (; *toys.optargs; toys.optargs++) {
    char *format = d.fmt;
    if (d.do_it(*toys.optargs)) {
      perror_msg("'%s'", *toys.optargs);
      continue;
    }
    for (; *format; format++) {
      if (*format != '%') {
        xputc(*format);
        continue;
      }
      format++;
      if (*format == 'n') xprintf("%s", *toys.optargs);
      else if (d.print_it(*format)) xputc(*format);
    }
    xputc('\n');
  }

  if(CFG_TOYBOX_FREE) free(TT.stat);
}
