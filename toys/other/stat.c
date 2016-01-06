/* stat.c : display file or file system status
 * Copyright 2012 <warior.linux@gmail.com>
 * Copyright 2013 <anand.sinha85@gmail.com>

USE_STAT(NEWTOY(stat, "<1c:f", TOYFLAG_BIN)) 

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
    %S  Best transfer size  |%t  Filesystem type    |%T  Filesystem type name
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
  xprintf("%s.%09ld", toybuf, ts->tv_nsec);
}

// Force numeric output to long long instead of manually typecasting everything
static void out(char c, long long val)
{
  sprintf(toybuf, "%%ll%c", c);
  printf(toybuf, val);
}

static void print_stat(char type)
{
  struct stat *stat = (struct stat *)&TT.stat;

  if (type == 'a') out('o', stat->st_mode&~S_IFMT);
  else if (type == 'A') {
    char str[11];

    mode_to_string(stat->st_mode, str);
    xprintf("%s", str);
  } else if (type == 'b') out('u', stat->st_blocks);
  else if (type == 'B') out('u', stat->st_blksize);
  else if (type == 'd') out('d', stat->st_dev);
  else if (type == 'D') out('x', stat->st_dev);
  else if (type == 'f') out('x', stat->st_mode);
  else if (type == 'F') {
    char *t = "character device\0directory\0block device\0" \
              "regular file\0symbolic link\0socket\0FIFO (named pipe)";
    int i, filetype = stat->st_mode & S_IFMT;

    for (i = 1; filetype != (i*8192) && i < 7; i++) t += strlen(t)+1;
    if (!stat->st_size && filetype == S_IFREG) t = "regular empty file";
    xprintf("%s", t);
  } else if (type == 'g') out('u', stat->st_gid);
  else if (type == 'G') xprintf("%8s", TT.group_name->gr_name);
  else if (type == 'h') out('u', stat->st_nlink);
  else if (type == 'i') out('u', stat->st_ino);
  else if (type == 'N') {
    xprintf("`%s'", *toys.optargs);
    if (S_ISLNK(stat->st_mode))
      if (0<readlink(*toys.optargs, toybuf, sizeof(toybuf)))
        xprintf(" -> `%s'", toybuf);
  } else if (type == 'o') out('u', stat->st_blksize);
  else if (type == 's') out('u', stat->st_size);
  else if (type == 'u') out('u', stat->st_uid);
  else if (type == 'U') xprintf("%8s", TT.user_name->pw_name);
  else if (type == 'x') date_stat_format((void *)&stat->st_atime);
  else if (type == 'X') out('u', stat->st_atime);
  else if (type == 'y') date_stat_format((void *)&stat->st_mtime);
  else if (type == 'Y') out('u', stat->st_mtime);
  else if (type == 'z') date_stat_format((void *)&stat->st_ctime);
  else if (type == 'Z') out('u', stat->st_ctime);
  else xprintf("?");
}

static void print_statfs(char type) {
  struct statfs *statfs = (struct statfs *)&TT.stat;

  if (type == 'a') out('u', statfs->f_bavail);
  else if (type == 'b') out('u', statfs->f_blocks);
  else if (type == 'c') out('u', statfs->f_files);
  else if (type == 'd') out('u', statfs->f_ffree);
  else if (type == 'f') out('u', statfs->f_bfree);
  else if (type == 'l') out('d', statfs->f_namelen);
  else if (type == 't') out('x', statfs->f_type);
  else if (type == 'T') {
    char *s = "unknown";
    struct {unsigned num; char *name;} nn[] = {
      {0xADFF, "affs"}, {0x5346544e, "ntfs"}, {0x1Cd1, "devpts"},
      {0x137D, "ext"}, {0xEF51, "ext2"}, {0xEF53, "ext3"},
      {0x1BADFACE, "bfs"}, {0x9123683E, "btrfs"}, {0x28cd3d45, "cramfs"},
      {0x3153464a, "jfs"}, {0x7275, "romfs"}, {0x01021994, "tmpfs"},
      {0x3434, "nilfs"}, {0x6969, "nfs"}, {0x9fa0, "proc"},
      {0x534F434B, "sockfs"}, {0x62656572, "sysfs"}, {0x517B, "smb"},
      {0x4d44, "msdos"}, {0x4006, "fat"}, {0x43415d53, "smackfs"},
      {0x73717368, "squashfs"}
    };
    int i;

    for (i=0; i<ARRAY_LEN(nn); i++)
      if (nn[i].num == statfs->f_type) s = nn[i].name;
    fputs(s, stdout);
  } else if (type == 'i')
    xprintf("%08x%08x", statfs->f_fsid.__val[0], statfs->f_fsid.__val[1]);
  else if (type == 's') out('d', statfs->f_frsize);
  else if (type == 'S') out('d', statfs->f_bsize);
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
      "Device: %Dh/%dd\t Inode: %i\t Links: %h\n"
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
