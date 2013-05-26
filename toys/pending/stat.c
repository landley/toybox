/* stat.c : display file or file system status
 * anand.sinha85@gmail.com
 * Copyright 2012 <warior.linux@gmail.com>

USE_STAT(NEWTOY(stat, "c:f", TOYFLAG_BIN)) 

config STAT
  bool stat
  default n
  help
    usage: stat [-f] [-c FORMAT] FILE...

    display file or file system status

    -f display file system status instead of file status
    -c use the specified FORMAT instead of the default;
       output a newline after each use of FORMAT

    The valid format sequences for files:
    %a     Access rights in octal
    %A     Access rights in human readable form
    %b     Number of blocks allocated
    %B     The size in bytes of each block
    %d     Device number in decimal
    %D     Device number in hex
    %f     Raw mode in hex
    %F     File type
    %g     Group ID of owner
    %G     Group name of owner
    %h     Number of hard links
    %i     Inode number
    %n     File name
    %N     Quoted file name with dereference if symbolic link
    %o     I/O block size
    %s     Total size, in bytes
    %u     User ID of owner
    %U     User name of owner
    %x     Time of last access
    %X     Time of last access as seconds since Epoch
    %y     Time of last modification
    %Y     Time of last modification as seconds since Epoch
    %z     Time of last change
    %Z     Time of last change as seconds since Epoch

    The valid format sequences for file systems:
    %a     Available blocks for unpriviledges user
    %b     Total number of blocks
    %c     Total number of inodes
    %d     Number of free inodes
    %f     Number of free blocks
    %i     File system ID
    %l     Maximum length of file names
    %n     File name
    %s     Fragment size
    %S     Optimal transfer block size
    %t     File system type
*/

#define FOR_stat
#include "toys.h"

GLOBALS(
	char *fmt;
	void *stat;
	char *file_type;
	struct passwd *user_name;
	struct group *group_name;
	char access_str[11];
)


static char * date_stat_format(time_t time)
{
  static char buf[36];

  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.000000000", localtime(&time));
  return buf;
}

static int print_stat(char type) {
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
      xprintf("%s", TT.file_type);
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
      xprintf("%s", date_stat_format(stat->st_atime));
      break;
    case 'X':
      xprintf("%llu", stat->st_atime);
      break;
    case 'y':
      xprintf("%s", date_stat_format(stat->st_mtime));
      break;
    case 'Y':
      xprintf("%llu", stat->st_mtime);
      break;
    case 'z':
      xprintf("%s", date_stat_format(stat->st_ctime));
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
  size_t i;
  struct {
    mode_t mode;
    char *str;
  } types[] = {
    {S_IFDIR, "directory"},
    {S_IFCHR, "character device"},
    {S_IFBLK, "block device"},
    {S_IFREG, "regular file"},
    {S_IFIFO, "FIFO (named pipe)"},
    {S_IFLNK, "symbolic link"},
    {S_IFSOCK, "socket"}
  };

  if (stat(path, statf) < 0) return 1;

  for (i = 0; i < sizeof(types)/sizeof(*types); i++)
    if ((statf->st_mode & S_IFMT) == types[i].mode) TT.file_type = types[i].str;
  if (!statf->st_size && (statf->st_mode & S_IFMT) == S_IFREG)
    TT.file_type = "regular empty file";

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
