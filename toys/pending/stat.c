/* stat.c : display file or file system status
 * anand.sinha85@gmail.com
 * Copyright 2012 <warior.linux@gmail.com>

USE_STAT(NEWTOY(stat, "LZc:f", TOYFLAG_BIN)) 

config STAT
  bool stat
  default n
  help
    Usage: stat [OPTION] FILE...
    display file or file system status
    -Z, --context
             print the security context information if available
    -f, --file-system
      display file system status instead of file status
    -c  --format=FORMAT
      use the specified FORMAT instead of the default; output a newline after each use of FORMAT
    --help display this help and exit
    The valid format sequences for files (without --file-system):
    %a     Access rights in octal
    %A     Access rights in human readable form
    %b     Number of blocks allocated (see
    %B     The size in bytes of each block reported by
    %d     Device number in decimal
    %D     Device number in hex
    %f     Raw mode in hex
    %F     File type
    %G     Group name of owner
    %h     Number of hard links
    %i     Inode number
    %n     File name
    %N     Quoted file name with dereference if symbolic link
    %o     I/O block size
    %s     Total size, in bytes
    %t     Major device type in hex
    %T     Minor device type in hex
    %u     User ID of owner
    %U     User name of owner
    %x     Time of last access
    %X     Time of last access as seconds since Epoch
    %y     Time of last modification
    %Y     Time of last modification as seconds since Epoch
    %z     Time of last change
    %Z     Time of last change as seconds since Epoch
*/

#define FOR_stat
#include "toys.h"

GLOBALS(
	char *fmt;
	char *access_str;
	char *file_type;
	struct passwd *user_name;
	struct group *group_name;
	struct stat *toystat;
	struct statfs *toystatfs;
)


static void do_stat(const char * file_name)
{
  TT.toystat = xmalloc(sizeof(struct stat));
  if (stat(file_name, TT.toystat) < 0) perror_exit("stat: '%s'", file_name);
}

static void do_statfs(const char * file_name)
{
  TT.toystatfs = xmalloc(sizeof(struct statfs));
  if (statfs(file_name, TT.toystatfs) < 0)
    perror_exit("statfs: '%s'", file_name);
}

static char * check_type_file(mode_t mode, size_t size)
{
  if (S_ISREG(mode)) {
    if (size) return "regular file";
    return "regular empty file";
  }
  if (S_ISDIR(mode)) return "directory"; 
  if (S_ISCHR(mode)) return "character device";
  if (S_ISBLK(mode)) return "block device";
  if (S_ISFIFO(mode)) return "FIFO (named pipe)";
  if (S_ISLNK(mode)) return "symbolic link";
  if (S_ISSOCK(mode)) return "socket";
}

static char * get_access_str(unsigned long permission, mode_t mode)
{
  static char access_string[11];
  char *s = access_string;
  char *rwx[] = {"---", "--x", "-w-", "-wx",
                 "r--", "r-x", "rw-", "rwx"};

  if (S_ISDIR(mode)) *s = 'd';
  else *s = '-';

  for (s += 7; s > access_string; s-=3) {
    memcpy(s, rwx[permission & 7], 3);
    permission >>= 3;
  }

  access_string[10] = '\0';
  return access_string;
}

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
        if (flag) xprintf("%lu", TT.toystatfs->f_bavail);
        else xprintf("%04lo",TT.toystat->st_mode & ~S_IFMT);
        break;
      case 'A':
        xprintf("%s",TT.access_str);
        break;
      case 'b':
        if (flag) xprintf("%lu", TT.toystatfs->f_blocks);
        else xprintf("%llu", TT.toystat->st_blocks);
        break;
      case 'B':
        xprintf("%lu", TT.toystat->st_blksize);
        break;
      case 'c':
        if (flag) xprintf("%lu", TT.toystatfs->f_files);
        break;
      case 'C':
        xprintf("Currently feature is not supported");
        break;
      case 'd':
        if (flag) xprintf("%lu", TT.toystatfs->f_ffree);
        else xprintf("%ldd", TT.toystat->st_dev);
        break;
      case 'D':
        xprintf("%llxh", TT.toystat->st_dev);
        break;
      case 'f':
        if (flag) xprintf("%lu", TT.toystatfs->f_bfree);
        else xprintf("%lx", TT.toystat->st_mode);
        break;
      case 'F':
        xprintf("%s", TT.file_type);
        break;
      case 'g':
        xprintf("%lu", TT.toystat->st_uid);
        break;
      case 'G':
        xprintf("%8s", TT.user_name->pw_name);
        break;
      case 'h':
        xprintf("%lu", TT.toystat->st_nlink);
        break;
      case 'i':
        if (flag)
          xprintf("%d%d", TT.toystatfs->f_fsid.__val[0], TT.toystatfs->f_fsid.__val[1]);
        else xprintf("%llu", TT.toystat->st_ino);
        break;
      case 'l':
        if (flag) xprintf("%ld", TT.toystatfs->f_namelen);
        break;
      case 'n':
        xprintf("%s", *toys.optargs);
        break;
      case 'N':
        xprintf("`%s'", *toys.optargs);
        break;
      case 'o':
        xprintf("%lu", TT.toystat->st_blksize);
        break;
      case 's':
        if (flag) xprintf("%d", TT.toystatfs->f_frsize);
        else xprintf("%llu", TT.toystat->st_size);
        break;
      case 'S':
        if (flag) xprintf("%d", TT.toystatfs->f_bsize);
        break;
      case 't':
        if (flag) xprintf("%lx", TT.toystatfs->f_type);
        break;
      case 'T':
        if (flag) xprintf("Needs to be implemented");
        break;
      case 'u':
        xprintf("%lu", TT.toystat->st_uid);
        break;
      case 'U':
        xprintf("%8s", TT.user_name->pw_name);
        break;
      case 'x':
        xprintf("%s", date_stat_format(TT.toystat->st_atime));
        break;
      case 'X':
        xprintf("%llu", TT.toystat->st_atime);
        break;
      case 'y':
        xprintf("%s", date_stat_format(TT.toystat->st_mtime));
        break;
      case 'Y':
        xprintf("%llu", TT.toystat->st_mtime);
        break;
      case 'z':
        xprintf("%s", date_stat_format(TT.toystat->st_ctime));
        break;
      case 'Z':
        xprintf("%llu", TT.toystat->st_ctime);
      default:
        xprintf("%c", *format);
        break;
    }
  }
  xprintf("\n");
}

void stat_main(void)
{
  int flag_f = toys.optflags & FLAG_f, flag_c = toys.optflags & FLAG_c;
  char *fmts[] = {
                  "  File: %N\n"
                  "  Size: %s\t Blocks: %S\t IO Blocks: %B\t%F\n"
                  "Device: %D\t Inode: %i\t Links: %h\n"
                  "Access: (%a/%A)\tUid: (%u/%U)\tGid: (%g/%G)\n"
                  "Access: %x\nModify: %y\nChange: %z",

                  "  File: \"%n\"\n"
                  "    ID: %i Namelen: %l    Type: %t\n"
                  "Block Size: %s    Fundamental block size: %S\n"
                  "Blocks: Total: %b\tFree: %f\tAvailable: %a\n"
                  "Inodes: Total: %c\tFree: %d",
           TT.fmt};

  if (toys.optflags & FLAG_Z) error_exit("SELinux feature has not been implemented so far..");
  if (!flag_f) {
    do_stat(*toys.optargs);
    // function to check the type/mode of file
    TT.file_type = check_type_file(TT.toystat->st_mode, TT.toystat->st_size);
    // check user and group name
    TT.user_name = getpwuid(TT.toystat->st_uid);
    TT.group_name = getgrgid(TT.toystat->st_gid);
    // function to get access in human readable format
    TT.access_str = get_access_str(TT.toystat->st_mode & ~S_IFMT, TT.toystat->st_mode);
  } else do_statfs(*toys.optargs);
  print_stat_format(fmts[!flag_c*flag_f+flag_c], flag_f);
}
