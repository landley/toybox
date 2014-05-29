/* makedevs.c - Make ranges of device files.
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard
 
USE_MAKEDEVS(NEWTOY(makedevs, "<1>1d:", TOYFLAG_USR|TOYFLAG_BIN))

config MAKEDEVS
  bool "makedevs"
  default n
  help
    usage: makedevs [-d device_table] rootdir
    Create a range of special files as specified in a device table.
    Device table entries take the following form:
    <name> <type> <mode> <uid> <gid> <major> <minor> <start> <increment> <count>
    Where name is the file name, type can be one of the following:
    b    Block device
    c    Character device
    d    Directory
    f    Regular file
    p    Fifo (named pipe)

    uid is the user id and gid is the group id for the target file.
    The rest of the entries (major, minor, etc.) apply to device 
    special files. A '-' may be used for blank entries.
*/
#define FOR_makedevs
#include "toys.h"

GLOBALS(
  char *fname;
)

void makedevs_main()
{
  int value, fd = 0, line_no;
  char *line = NULL;

  if (toys.optflags & FLAG_d) {
    xprintf("rootdir = %s\ntable = %s\n", *toys.optargs, 
        (!strcmp(TT.fname, "-")) ? "<stdin>": TT.fname);
    fd = (!strcmp(TT.fname, "-")) ? 0 : xopen(TT.fname, O_RDONLY);
  } else xprintf("rootdir = %s\ntable = %s\n", *toys.optargs, "<stdin>");

  xchdir(*toys.optargs);  // root dir
  for (line_no = 0; (line = get_line(fd)); free(line)) {
    char type, str[64], user[64], group [64], *node = str, *ptr = line;
    unsigned int mode = 0755, major = 0, minor = 0, cnt = 0, incr = 0, 
                 st_val = 0, i;
    uid_t uid;
    gid_t gid;
    dev_t dev;
    struct stat st;

    line_no++;
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    if (!*ptr || *ptr == '#') continue;
    sscanf(line, "%63s %c %o %63s %63s %u %u %u %u %u", node, &type, &mode,
        user, group, &major, &minor, &st_val, &incr, &cnt);
    if ((major | minor | st_val | cnt | incr) > 255) {
      error_msg("invalid line %d: '%s'", line_no, line);
      continue;
    }

    if (*user) {
      struct passwd *usr;

      if (!(usr = getpwnam(user)) && isdigit(*user)) {
        sscanf (user, "%d", &value);
        usr = xgetpwuid(value);
      }
      if (!usr) error_exit("bad user '%s'", user);
      uid = usr->pw_uid;
    } else uid = getuid();

    if (*group) {
      struct group *grp;

      if (!(grp = getgrnam(group)) && isdigit(*group)) {
        sscanf (group, "%d", &value);
        grp = getgrgid(value);
      }
      if (!grp) error_exit("bad group '%s'", group);
      gid = grp->gr_gid;
    } else gid = getgid();

    if (*node == '/') node++; // using relative path
    switch (type) {
      case 'd':
        if (mkpathat(AT_FDCWD, node, mode, 3)) 
          perror_msg("can't create directory '%s'", node);
        else if (chown(node, uid, gid) || chmod(node, mode)) 
          perror_msg("line %d: can't chown/chmod '%s'", line_no, node);
        break;
      case 'f': 
        if ((stat(node, &st) || !S_ISREG(st.st_mode)))
          perror_msg("line %d: regular file '%s' does not exist",
              line_no, node);
        else if (chown(node, uid, gid) || chmod(node, mode))
          perror_msg("line %d: can't chown/chmod '%s'", line_no, node);
        break;
      case 'p': mode |= S_IFIFO; goto CREATENODE;
      case 'c': mode |= S_IFCHR; goto CREATENODE;
      case 'b': mode |= S_IFBLK;
CREATENODE:
        if (cnt) --cnt;
        for (i = st_val; i <= st_val + cnt; i++) {
          sprintf(toybuf, cnt ? "%s%u" : "%s", node, i);
          dev = makedev(major, minor + (i - st_val) * incr);
          if (mknod(toybuf, mode, dev)) 
            perror_msg("line %d: can't create node '%s'", line_no, toybuf);
          else if (chown(toybuf, uid, gid) || chmod(toybuf, mode)) 
            perror_msg("line %d: can't chown/chmod '%s'", line_no, toybuf);
        }
        break;
      default: 
        error_msg("line %d: file type %c is unsupported", line_no, type);
        break;
    }
  }
  xclose(fd);
}
