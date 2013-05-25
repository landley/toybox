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
    %s     Fragment size
    %S     Optimal transfer block size
    %t     File system type
*/

#define FOR_stat
#include "toys.h"

GLOBALS(
	char *fmt;
	char access_str[11];
	char *file_type;
	struct passwd *user_name;
	struct group *group_name;
	struct stat *stat;
	struct statfs *statfs;
)


static char * date_stat_format(time_t time)
{
  static char buf[36];

  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.000000000", localtime(&time));
  return buf;
}

static void print_stat_format(char *format, int flag)
{
  for (; *format; format++) {
    if (*format != '%') {
      xprintf("%c", *format);
      continue;
    }
    format++;
    switch (*format) {
      case 'a':
        if (flag) xprintf("%lu", TT.statfs->f_bavail);
        else xprintf("%04lo",TT.stat->st_mode & ~S_IFMT);
        break;
      case 'A':
        xprintf("%s",TT.access_str);
        break;
      case 'b':
        if (flag) xprintf("%lu", TT.statfs->f_blocks);
        else xprintf("%llu", TT.stat->st_blocks);
        break;
      case 'B':
        xprintf("%lu", TT.stat->st_blksize);
        break;
      case 'c':
        if (flag) xprintf("%lu", TT.statfs->f_files);
        break;
      case 'd':
        if (flag) xprintf("%lu", TT.statfs->f_ffree);
        else xprintf("%ldd", TT.stat->st_dev);
        break;
      case 'D':
        xprintf("%llxh", TT.stat->st_dev);
        break;
      case 'f':
        if (flag) xprintf("%lu", TT.statfs->f_bfree);
        else xprintf("%lx", TT.stat->st_mode);
        break;
      case 'F':
        xprintf("%s", TT.file_type);
        break;
      case 'g':
        xprintf("%lu", TT.stat->st_gid);
        break;
      case 'G':
        xprintf("%8s", TT.user_name->pw_name);
        break;
      case 'h':
        xprintf("%lu", TT.stat->st_nlink);
        break;
      case 'i':
        if (flag)
          xprintf("%x%x", TT.statfs->f_fsid.__val[0], TT.statfs->f_fsid.__val[1]);
        else xprintf("%llu", TT.stat->st_ino);
        break;
      case 'l':
        if (flag) xprintf("%ld", TT.statfs->f_namelen);
        break;
      case 'n':
        xprintf("%s", *toys.optargs);
        break;
      case 'N':
        xprintf("`%s'", *toys.optargs);
        break;
      case 'o':
        xprintf("%lu", TT.stat->st_blksize);
        break;
      case 's':
        if (flag) xprintf("%d", TT.statfs->f_frsize);
        else xprintf("%llu", TT.stat->st_size);
        break;
      case 'S':
        if (flag) xprintf("%d", TT.statfs->f_bsize);
        break;
      case 't':
        if (flag) xprintf("%lx", TT.statfs->f_type);
        break;
      case 'u':
        xprintf("%lu", TT.stat->st_uid);
        break;
      case 'U':
        xprintf("%8s", TT.user_name->pw_name);
        break;
      case 'x':
        xprintf("%s", date_stat_format(TT.stat->st_atime));
        break;
      case 'X':
        xprintf("%llu", TT.stat->st_atime);
        break;
      case 'y':
        xprintf("%s", date_stat_format(TT.stat->st_mtime));
        break;
      case 'Y':
        xprintf("%llu", TT.stat->st_mtime);
        break;
      case 'z':
        xprintf("%s", date_stat_format(TT.stat->st_ctime));
        break;
      case 'Z':
        xprintf("%llu", TT.stat->st_ctime);
      default:
        xprintf("%c", *format);
        break;
    }
  }
  xprintf("\n");
}

int do_stat(char *path)
{
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

  if (stat(path, TT.stat) < 0) return 1;

  for (i = 0; i < sizeof(types)/sizeof(*types); i++)
    if((TT.stat->st_mode&S_IFMT) == types[i].mode) TT.file_type = types[i].str;
  if(!TT.stat->st_size && (TT.stat->st_mode & S_IFMT) == S_IFREG)
    TT.file_type = "regular empty file";

  // check user and group name
  TT.user_name = getpwuid(TT.stat->st_uid);
  TT.group_name = getgrgid(TT.stat->st_gid);
  // function to get access in human readable format
  format_mode(&TT.access_str, TT.stat->st_mode);
  return 0;
}

int do_statfs(char *path)
{
  return statfs(path, TT.statfs) < 0;
}

void stat_main(void)
{
  int flag_f = toys.optflags & FLAG_f;
  char *fmt;
  int (*do_it)(char*);

  if (!flag_f) {
    TT.stat = xmalloc(sizeof(struct stat));
    fmt = "  File: %N\n"
          "  Size: %s\t Blocks: %b\t IO Blocks: %B\t%F\n"
          "Device: %D\t Inode: %i\t Links: %h\n"
          "Access: (%a/%A)\tUid: (%u/%U)\tGid: (%g/%G)\n"
          "Access: %x\nModify: %y\nChange: %z";
    do_it = do_stat;
  } else {
    TT.statfs = xmalloc(sizeof(struct statfs));
    fmt = "  File: \"%n\"\n"
          "    ID: %i Namelen: %l    Type: %t\n"
          "Block Size: %s    Fundamental block size: %S\n"
          "Blocks: Total: %b\tFree: %f\tAvailable: %a\n"
          "Inodes: Total: %c\tFree: %d";
    do_it = do_statfs;
  }
  if (toys.optflags & FLAG_c) fmt = TT.fmt;

  for (; *toys.optargs; toys.optargs++) {
    if (do_it(*toys.optargs)) {
      perror_msg("'%s'", *toys.optargs);
      continue;
    }
    print_stat_format(fmt, flag_f);
  }
}
