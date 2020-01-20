/* lsattr.c - List file attributes on a Linux second extended file system.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.
 *
 * TODO cleanup

USE_LSATTR(NEWTOY(lsattr, "vldaR", TOYFLAG_BIN))
USE_CHATTR(NEWTOY(chattr, NULL, TOYFLAG_BIN))

config LSATTR
  bool "lsattr"
  default y
  help
    usage: lsattr [-Radlv] [file...]

    List file attributes on a Linux second extended file system.
    Flag letters are defined in chattr help.

    -R	Recursively list attributes of directories and their contents
    -a	List all files in directories, including files that start with '.'
    -d	List directories like other files, rather than listing their contents
    -l	List long flag names
    -v	List the file's version/generation number

config CHATTR
  bool "chattr"
  default y
  help
    usage: chattr [-R] [-+=AacDdijsStTu] [-v version] [file...]

    Change file attributes on a Linux second extended file system.

    -R	Recurse
    -v	Set the file's version/generation number

    Operators:
      '-' Remove attributes
      '+' Add attributes
      '=' Set attributes

    Attributes:
      A  Don't track atime
      a  Append mode only
      C  No copy on write
      c  Enable compress
      D  Write dir contents synchronously
      d  Don't backup with dump
      F  Case-insensitive directory lookups (casefold)
      i  Cannot be modified (immutable)
      j  Write all data to journal first
      P  Project hierarchy
      S  Write file contents synchronously
      s  Zero disk storage when deleted
      T  Top of directory hierarchy
      t  Disable tail-merging of partial blocks with other files
      u  Allow file to be undeleted
*/
#define FOR_lsattr
#include "toys.h"
#include <linux/fs.h>

#define FS_PROJINHERT_FL 0x20000000 // Linux 4.5
#define FS_CASEFOLD_FL   0x40000000 // Linux 5.4

static struct ext2_attr {
  char *name;
  unsigned long flag;
  char opt;
} e2attrs[] = {
  {"No_Atime",                      FS_NOATIME_FL,      'A'},
  {"Append_Only",                   FS_APPEND_FL,       'a'},
  {"No_COW",                        FS_NOCOW_FL,        'C'},
  {"Compression_Requested",         FS_COMPR_FL,        'c'},
  {"Synchronous_Directory_Updates", FS_DIRSYNC_FL,      'D'},
  {"No_Dump",                       FS_NODUMP_FL,       'd'},
  {"Casefold",                      FS_CASEFOLD_FL,     'F'},
  {"Indexed_directory",             FS_INDEX_FL,        'I'},
  {"Immutable",                     FS_IMMUTABLE_FL,    'i'},
  {"Journaled_Data",                FS_JOURNAL_DATA_FL, 'j'},
  {"Project_Hierarchy",             FS_PROJINHERIT_FL,  'P'},
  {"Synchronous_Updates",           FS_SYNC_FL,         'S'},
  {"Secure_Deletion",               FS_SECRM_FL,        's'},
  {"Top_of_Directory_Hierarchies",  FS_TOPDIR_FL,       'T'},
  {"No_Tailmerging",                FS_NOTAIL_FL,       't'},
  {"Undelete",                      FS_UNRM_FL,         'u'},
  {NULL,                            -1,                   0},
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

  if (FLAG(v)) {
    if (ioctl(fd, FS_IOC_GETVERSION, (void*)&version) < 0) goto LABEL2;
    xprintf("%5lu ", version);
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
  unsigned long add, rm, set, version;
  unsigned char vflag, recursive;
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
          } else if (*ptr == 'v') {// get version from next argv.
            char *endptr;

            errno = 0;
            arg = *(*argv += 1);
            if (!arg) help_exit("bad -v");
            if (*arg == '-') perror_exit("Invalid Number '%s'", arg);
            chattr.version = strtoul(arg, &endptr, 0);
            if (errno || *endptr) perror_exit("bad version '%s'", arg);
            chattr.vflag = 1;
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
  // set file version
  if (chattr.vflag && (ioctl(fd, FS_IOC_SETVERSION, &chattr.version)<0))
    perror_msg("while setting version on '%s'", fpath);
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
  if (!(chattr.add || chattr.rm || chattr.set || chattr.vflag))
    error_exit("need '-v', '=', '-' or '+'");
  for (; *argv; argv++) dirtree_read(*argv, update_attr);
  toys.exitval = 0; //always set success at this point.
}
