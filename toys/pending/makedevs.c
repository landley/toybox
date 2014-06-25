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

    -d	file containing device table (default reads from stdin)

    Each line of of the device table has the fields:
    <name> <type> <mode> <uid> <gid> <major> <minor> <start> <increment> <count>
    Where name is the file name, and type is one of the following:

    b	Block device
    c	Character device
    d	Directory
    f	Regular file
    p	Named pipe (fifo)

    Other fields specify permissions, user and group id owning the file,
    and additional fields for device special files. Use '-' for blank entries,
    unspecified fields are treated as '-'.
*/

#define FOR_makedevs
#include "toys.h"

GLOBALS(
  char *fname;
)

void makedevs_main()
{
  int value, fd = 0, line_no, i;
  char *line = NULL;

  // Open file and chdir, verbosely
  xprintf("rootdir = %s\n", *toys.optargs);
  if (toys.optflags & FLAG_d && strcmp(TT.fname, "-")) {
    fd = xopen(TT.fname, O_RDONLY);
    xprintf("table = %s\n", TT.fname);
  } else xprintf("table = <stdin>");
  xchdir(*toys.optargs);

  for (line_no = 0; (line = get_line(fd)); free(line)) {
    char type=0, str[64], user[64], group[64], *node = str, *ptr = line;
    unsigned int mode = 0755, major = 0, minor = 0, cnt = 0, incr = 0, 
                 st_val = 0;
    uid_t uid;
    gid_t gid;
    struct stat st;

    line_no++;
    while (isspace(*ptr)) ptr++;
    if (!*ptr || *ptr == '#') continue;

    while (*ptr && !isspace(*ptr)) ptr++;
    if (*ptr) *ptr++ = 0;
    *user = *group = 0;
    sscanf(ptr, "%c %o %63s %63s %u %u %u %u %u", &type, &mode,
           user, group, &major, &minor, &st_val, &incr, &cnt);

    // type order here needs to line up with actions[] order.
    i = stridx("pcbdf", type);
    if (i == -1) {
      error_msg("line %d: bad type %c", line_no, type);
      continue;
    } else mode |= (mode_t[]){S_IFIFO, S_IFCHR, S_IFBLK, 0, 0}[i];

    if (*user) {
      struct passwd *usr;

      if (!(usr = getpwnam(user)) && isdigit(*user)) {
        sscanf(user, "%u", &value);
        usr = xgetpwuid(value);
      }
      if (!usr) error_exit("bad user '%s'", user);
      uid = usr->pw_uid;
    } else uid = getuid();

    if (*group) {
      struct group *grp;

      if (!(grp = getgrnam(group)) && isdigit(*group)) {
        sscanf (group, "%u", &value);
        grp = getgrgid(value);
      }
      if (!grp) error_exit("bad group '%s'", group);
      gid = grp->gr_gid;
    } else gid = getgid();

    while (*node == '/') node++; // using relative path
    if (type == 'd') {
      if (mkpathat(AT_FDCWD, node, mode, 3))  {
        perror_msg("can't create directory '%s'", node);
        continue;
      }
    } else if (type == 'f') {
      if (stat(node, &st) || !S_ISREG(st.st_mode)) {
        perror_msg("line %d: regular file '%s' does not exist", line_no, node);
        continue;
      }
    } else {
      if (cnt) --cnt;
      for (i = 0; i <= cnt; i++) {
        sprintf(toybuf, cnt ? "%s%u" : "%s", node, st_val + i);
        if (mknod(toybuf, mode, makedev(major, minor + i*incr))) {
          perror_msg("line %d: can't create node '%s'", line_no, toybuf);
          continue;
        }
      }
    }
    if (chown(toybuf, uid, gid) || chmod(toybuf, mode)) 
      perror_msg("line %d: can't chown/chmod '%s'", line_no, toybuf);
  }
  xclose(fd);
}
