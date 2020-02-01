/* lsattr.c - List file attributes on a Linux second extended file system.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.
 *
 * TODO cleanup

USE_LSATTR(NEWTOY(lsattr, "vpldaR", TOYFLAG_BIN))
USE_CHATTR(NEWTOY(chattr, NULL, TOYFLAG_BIN))

config LSATTR
  bool "lsattr"
  default y
  help
    usage: lsattr [-Radlpv] [FILE...]

    List file attributes on a Linux file system.
    Flag letters are defined in chattr help.

    -R	Recursively list attributes of directories and their contents
    -a	List all files in directories, including files that start with '.'
    -d	List directories like other files, rather than listing their contents
    -l	List long flag names
    -p	List the file's project number
    -v	List the file's version/generation number

config CHATTR
  bool "chattr"
  default y
  help
    usage: chattr [-R] [-+=AacDdijsStTu] [-p PROJID] [-v VERSION] [FILE...]

    Change file attributes on a Linux file system.

    -R	Recurse
    -p	Set the file's project number
    -v	Set the file's version/generation number

    Operators:
      '-' Remove attributes
      '+' Add attributes
      '=' Set attributes

    Attributes:
      A  No atime                     a  Append only
      C  No COW                       c  Compression
      D  Synchronous dir updates      d  No dump
      E  Encrypted                    e  Extents
      F  Case-insensitive (casefold)
      I  Indexed directory            i  Immutable
      j  Journal data
      N  Inline data in inode
      P  Project hierarchy
      S  Synchronous file updates     s  Secure delete
      T  Top of dir hierarchy         t  No tail-merging
      u  Allow undelete
      V  Verity
*/
#define FOR_lsattr
#include "toys.h"
#include <linux/fs.h>

#define FS_PROJINHERT_FL 0x20000000 // Linux 4.5
#define FS_CASEFOLD_FL   0x40000000 // Linux 5.4
#define FS_VERITY_FL     0x00100000 // Linux 5.4

// Linux 4.5
struct fsxattr_4_5 {
  unsigned fsx_xflags;
  unsigned fsx_extsize;
  unsigned fsx_nextents;
  unsigned fsx_projid;
  unsigned fsx_cowextsize;
  char fsx_pad[8];
};
#define FS_IOC_FSGETXATTR_4_5 _IOR('X', 31, struct fsxattr_4_5)
#define FS_IOC_FSSETXATTR_4_5 _IOW('X', 32, struct fsxattr_4_5)

static struct ext2_attr {
  char *name;
  unsigned long flag;
  char opt;
} e2attrs[] = {
  // Do not sort! These are in the order that lsattr outputs them.
  {"Secure_Deletion",               FS_SECRM_FL,        's'},
  {"Undelete",                      FS_UNRM_FL,         'u'},
  {"Synchronous_Updates",           FS_SYNC_FL,         'S'},
  {"Synchronous_Directory_Updates", FS_DIRSYNC_FL,      'D'},
  {"Immutable",                     FS_IMMUTABLE_FL,    'i'},
  {"Append_Only",                   FS_APPEND_FL,       'a'},
  {"No_Dump",                       FS_NODUMP_FL,       'd'},
  {"No_Atime",                      FS_NOATIME_FL,      'A'},
  {"Compression_Requested",         FS_COMPR_FL,        'c'},
  {"Encrypted",                     FS_ENCRYPT_FL,      'E'},
  {"Journaled_Data",                FS_JOURNAL_DATA_FL, 'j'},
  {"Indexed_directory",             FS_INDEX_FL,        'I'},
  {"No_Tailmerging",                FS_NOTAIL_FL,       't'},
  {"Top_of_Directory_Hierarchies",  FS_TOPDIR_FL,       'T'},
  {"Extents",                       FS_EXTENT_FL,       'e'},
  {"No_COW",                        FS_NOCOW_FL,        'C'},
  {"Casefold",                      FS_CASEFOLD_FL,     'F'},
  {"Inline_Data",                   FS_INLINE_DATA_FL,  'N'},
  {"Project_Hierarchy",             FS_PROJINHERIT_FL,  'P'},
  {"Verity",                        FS_VERITY_FL,       'V'},
  {NULL,                            0,                  0},
};

// Get file flags on a Linux second extended file system.
static int ext2_getflag(int fd, struct stat *sb, unsigned long *flag)
{
  if(!S_ISREG(sb->st_mode) && !S_ISDIR(sb->st_mode)) {
    errno = EOPNOTSUPP;
    return -1;
  }
  return (ioctl(fd, FS_IOC_GETFLAGS, (void*)flag));
}

static void print_file_attr(char *path)
{
  unsigned long flag = 0, version = 0;
  int fd;
  struct stat sb;

  if (!stat(path, &sb) && !S_ISREG(sb.st_mode) && !S_ISDIR(sb.st_mode)) {
    errno = EOPNOTSUPP;
    goto LABEL1;
  }
  if (-1 == (fd=open(path, O_RDONLY | O_NONBLOCK))) goto LABEL1;

  if (FLAG(p)) {
    struct fsxattr_4_5 fsx;

    if (ioctl(fd, FS_IOC_FSGETXATTR_4_5, &fsx)) goto LABEL2;
    xprintf("%5u ", fsx.fsx_projid);
  }
  if (FLAG(v)) {
    if (ioctl(fd, FS_IOC_GETVERSION, (void*)&version) < 0) goto LABEL2;
    xprintf("%-10lu ", version);
  }

  if (ext2_getflag(fd, &sb, &flag) < 0) perror_msg("reading flags '%s'", path);
  else {
    struct ext2_attr *ptr = e2attrs;

    if (FLAG(l)) {
      int name_found = 0;

      xprintf("%-50s ", path);
      for (; ptr->name; ptr++) {
        if (flag & ptr->flag) {
          if (name_found) xprintf(", "); //for formatting.
          xprintf("%s", ptr->name);
          name_found = 1;
        }
      }
      if (!name_found) xprintf("---");
      xputc('\n');
    } else {
      int index = 0;

      for (; ptr->name; ptr++)
        toybuf[index++] = (flag & ptr->flag) ? ptr->opt : '-';
      toybuf[index] = '\0';
      xprintf("%s %s\n", toybuf, path);
    }
  }
  xclose(fd);
  return;
LABEL2: xclose(fd);
LABEL1: perror_msg("reading '%s'", path);
}

// Get directory information.
static int retell_dir(struct dirtree *root)
{
  char *fpath = NULL;

  if (root->again) {
    xputc('\n');
    return 0;
  }
  if (S_ISDIR(root->st.st_mode) && !root->parent)
    return (DIRTREE_RECURSE | DIRTREE_COMEAGAIN);

  fpath = dirtree_path(root, NULL);
  //Special case: with '-a' option and '.'/'..' also included in printing list.
  if ((root->name[0] != '.') || FLAG(a)) {
    print_file_attr(fpath);
    if (S_ISDIR(root->st.st_mode) && FLAG(R) && dirtree_notdotdot(root)) {
      xprintf("\n%s:\n", fpath);
      free(fpath);
      return (DIRTREE_RECURSE | DIRTREE_COMEAGAIN);
    }
  }
  free(fpath);
  return 0;
}

void lsattr_main(void)
{
  if (!*toys.optargs) dirtree_read(".", retell_dir);
  else
    for (; *toys.optargs;  toys.optargs++) {
      struct stat sb;

      if (lstat(*toys.optargs, &sb)) perror_msg("stat '%s'", *toys.optargs);
      else if (S_ISDIR(sb.st_mode) && !FLAG(d))
        dirtree_read(*toys.optargs, retell_dir);
      else print_file_attr(*toys.optargs);// to handle "./Filename" or "./Dir"
    }
}

// Switch gears from lsattr to chattr.
#define CLEANUP_lsattr
#define FOR_chattr
#include "generated/flags.h"

static struct _chattr {
  unsigned long add, rm, set, projid, version;
  unsigned char pflag, vflag, recursive;
} chattr;

// Set file flags on a Linux second extended file system.
static inline int ext2_setflag(int fd, struct stat *sb, unsigned long flag)
{
  if (!S_ISREG(sb->st_mode) && !S_ISDIR(sb->st_mode)) {
    errno = EOPNOTSUPP;
    return -1;
  }
  return (ioctl(fd, FS_IOC_SETFLAGS, (void*)&flag));
}

static unsigned long get_flag_val(char ch)
{
  struct ext2_attr *ptr = e2attrs;

  for (; ptr->name; ptr++)
    if (ptr->opt == ch) return ptr->flag;
  help_exit("bad '%c'", ch);
}

// Parse command line argument and fill the chattr structure.
static void parse_cmdline_arg(char ***argv)
{
  char *arg = **argv, *ptr = NULL;

  while (arg) {
    switch (arg[0]) {
      case '-':
        for (ptr = ++arg; *ptr; ptr++) {
          if (*ptr == 'R') {
            chattr.recursive = 1;
            continue;
          } else if (*ptr == 'p' || *ptr == 'v') {
            unsigned val;

            arg = *(*argv += 1);
            if (!arg) help_exit("missing arg to -%c", *ptr);

            val = atolx_range(arg, 0, UINT_MAX);
            if (*ptr == 'v') {
              chattr.version = val;
              chattr.vflag = 1;
            } else {
              chattr.projid = val;
              chattr.pflag = 1;
            }
            continue;
          } else chattr.rm |= get_flag_val(*ptr);
        }
        break;
      case '+':
        for (ptr = ++arg; *ptr; ptr++)
          chattr.add |= get_flag_val(*ptr);
        break;
      case '=':
        for (ptr = ++arg; *ptr; ptr++)
          chattr.set |= get_flag_val(*ptr);
        break;
      default: return;
    }
    arg = *(*argv += 1);
  }
}

// Update attribute of given file.
static int update_attr(struct dirtree *root)
{
  unsigned long fval = 0;
  char *fpath = NULL;
  int fd;

  if (!dirtree_notdotdot(root)) return 0;

  /*
   * if file is a link and recursive is set or file is not regular+link+dir
   * (like fifo or dev file) then escape the file.
   */
  if ((S_ISLNK(root->st.st_mode) && chattr.recursive)
    || (!S_ISREG(root->st.st_mode) && !S_ISLNK(root->st.st_mode)
      && !S_ISDIR(root->st.st_mode)))
    return 0;

  fpath = dirtree_path(root, NULL);
  if (-1 == (fd=open(fpath, O_RDONLY | O_NONBLOCK))) {
    free(fpath);
    return DIRTREE_ABORT;
  }
  // Get current attr of file.
  if (ext2_getflag(fd, &(root->st), &fval) < 0) {
    perror_msg("read flags of '%s'", fpath);
    free(fpath);
    xclose(fd);
    return DIRTREE_ABORT;
  }
  if (chattr.set) { // for '=' operator.
    if (ext2_setflag(fd, &(root->st), chattr.set) < 0)
      perror_msg("setting flags '%s'", fpath);
  } else { // for '-' / '+' operator.
    fval &= ~(chattr.rm);
    fval |= chattr.add;
    if (!S_ISDIR(root->st.st_mode)) fval &= ~FS_DIRSYNC_FL;
    if (ext2_setflag(fd, &(root->st), fval) < 0)
      perror_msg("setting flags '%s'", fpath);
  }

  // (FS_IOC_SETVERSION works all the way back to 2.6, but FS_IOC_FSSETXATTR
  // isn't available until 4.5.)
  if (chattr.vflag && (ioctl(fd, FS_IOC_SETVERSION, &chattr.version)<0))
    perror_msg("while setting version on '%s'", fpath);

  if (chattr.pflag) {
    struct fsxattr_4_5 fsx;

    if (ioctl(fd, FS_IOC_FSGETXATTR_4_5, &fsx))
      perror_exit("%s: FS_IOC_FSGETXATTR failed", fpath);
    fsx.fsx_projid = chattr.projid;
    if (ioctl(fd, FS_IOC_FSSETXATTR_4_5, &fsx))
      perror_exit("%s: FS_IOC_FSSETXATTR failed", fpath);
  }

  free(fpath);
  xclose(fd);
  return (S_ISDIR(root->st.st_mode) && chattr.recursive) ? DIRTREE_RECURSE : 0;
}

void chattr_main(void)
{
  char **argv = toys.optargs;

  memset(&chattr, 0, sizeof(struct _chattr));
  parse_cmdline_arg(&argv);
  if (!*argv) help_exit("no file");
  if (chattr.set && (chattr.add || chattr.rm))
    error_exit("no '=' with '-' or '+'");
  if (chattr.rm & chattr.add) error_exit("set/unset same flag");
  if (!(chattr.add || chattr.rm || chattr.set || chattr.pflag || chattr.vflag))
    error_exit("need '-p', '-v', '=', '-', or '+'");
  for (; *argv; argv++) dirtree_read(*argv, update_attr);
  toys.exitval = 0; //always set success at this point.
}
