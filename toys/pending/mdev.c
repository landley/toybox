/* mdev.c - Populate /dev directory and handle hotplug events
 *
 * Copyright 2005, 2008 Rob Landley <rob@landley.net>
 * Copyright 2005 Frank Sorenson <frank@tuxrocks.com>

USE_MDEV(NEWTOY(mdev, "s", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_UMASK))

config MDEV
  bool "mdev"
  default n
  help
    usage: mdev [-s]

    Create devices in /dev using information from /sys.

    -s	Scan all entries in /sys to populate /dev.

config MDEV_CONF
  bool "Configuration file for mdev"
  default y
  depends on MDEV
  help
    The mdev config file (/etc/mdev.conf) contains lines that look like:
    hd[a-z][0-9]* 0:3 660

    Each line must contain three whitespace separated fields. The first
    field is a regular expression matching one or more device names, and
    the second and third fields are uid:gid and file permissions for
    matching devies.
*/

#define FOR_mdev
#include "toys.h"

// todo, open() block devices to trigger partition scanning.

GLOBALS(
  char *devname;
  int root_maj, root_min, verbose;
)


// mknod in /dev based on a path like "/sys/block/hda/hda1"
static void make_device(char *path, char *operation)
{
  char *device_name, *s, *temp, *alias = NULL, *cmd = NULL, buf[PATH_MAX];
  char sign = 0, op_pref = 0, *str2 = NULL, *ln = NULL;
  int major, minor, type, len, fd, ufd, mode = 0660;
  uid_t uid = 0;
  gid_t gid = 0;

  // Try to read major/minor string

  temp = strrchr(path, '/');
  fd = open(path, O_RDONLY);
  *temp = 0;
  temp = toybuf;
  len = read(fd, temp, 64);
  close(fd);
  if (!strcmp(operation, "add") && len < 1) return;
  temp[len] = 0;
  major = minor = 0;
  if (sscanf(temp, "%u:%u", &major, &minor) != 2) major = -1;

  memset(buf, 0, sizeof(buf));
  device_name = TT.devname;
  if (!device_name) {
    sprintf(buf,"%s/uevent", path);
    if ((ufd = open(buf, O_RDONLY)) >= 0) {
      for (; (ln = get_line(ufd)); free(ln)) {
        if (strstr(ln, "DEVNAME=")) {
          device_name = ln + strlen("DEVNAME=") ;
          break;
        }  
      }
      close(ufd);
    }
  }
  if (!device_name) device_name = strrchr(path, '/') + 1;
  type = S_IFCHR;
  if (strstr(path, "/block/")) type =  S_IFBLK;

  // If we have a config file, look up permissions for this device

  if (CFG_MDEV_CONF) {
    char *conf, *pos, *end;

    // mmap the config file
    if (-1 != (fd = open("/etc/mdev.conf", O_RDONLY))) {
      len = fdlength(fd);
      conf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
      if (conf) {
        int line = 0;

        // Loop through lines in mmaped file
        for (pos = conf; pos-conf < len;) {
          int field;
          char *end2;

          line++;
          // find end of this line
          for(end = pos; end-conf < len && *end != '\n'; end++);

          // Fields: regex uid:gid mode [alias] [cmd]
          for (field = 3; field >= 0; field--) {
            // Skip whitespace
            while (pos < end && isspace(*pos)) pos++;
            if (pos == end || *pos == '#') break;
            for (end2 = pos;
                end2 < end && !isspace(*end2) && *end2 != '#'; end2++);
            switch(field) {
              // Regex to match this device
              case 3:
                {
                  char *str = strndup(pos, end2-pos), *regex;
                  regex_t match;
                  regmatch_t off;
                  int result;

                  if (*str == '$') {
                    char *p = strchr(str, '=');

                    if (!p) {
                      free(str);
                      error_msg("bad $envvar=regex");
                      goto end_line;
                    } else {
                      p++;
                      regex = xstrdup(p);
                      free(str);
                    }
                  } else {
                    regex = xstrdup(str);
                    free(str);
                  }
                  // Is this it?
                  if (*regex == '@') {
                    int maj, min1, min2;

                    int ret = sscanf(regex, "@%u,%u-%u", &maj, &min1, &min2);
                    free(regex);
                    if (ret < 2 || maj < 0) {
                      error_msg("bad @maj,min @line %d",line);
                      goto end_line;
                    }
                    if (ret == 2) min2 = min1;
                    if (!(major == maj && ( min1 <= minor || minor <= min2)))
                      goto end_line;
                  } else {
                    xregcomp(&match, regex, REG_EXTENDED);
                    result = regexec(&match, device_name, 1, &off, 0);
                    regfree(&match);
                    free(regex);

                    // If not this device, skip rest of line
                    if (result || off.rm_so
                        || off.rm_eo != strlen(device_name))
                      goto end_line;
                  }
                  break;
                }
                // uid:gid
              case 2:
                {
                  char *s2;

                  // Find :
                  for(s = pos; s < end2 && *s != ':'; s++);
                  if (s == end2) goto end_line;

                  // Parse UID
                  uid = strtoul(pos,&s2,10);
                  if (s != s2) {
                    struct passwd *pass;
                    char *str = xstrndup(pos, s-pos);

                    pass = getpwnam(str);
                    free(str);
                    if (!pass) goto end_line;
                    uid = pass->pw_uid;
                  }
                  s++;
                  // parse GID
                  gid = strtoul(s,&s2,10);
                  if (end2 != s2) {
                    struct group *grp;
                    char *str = xstrndup(s, end2-s);

                    grp = getgrnam(str);
                    free(str);
                    if (!grp) goto end_line;
                    gid = grp->gr_gid;
                  }
                  break;
                }
                // mode
              case 1:
                {
                  mode = strtoul(pos, &pos, 8);
                  if (pos != end2) goto end_line;
                  //goto found_device;
                  break;
                }
                // handle 4th and 5th field
              case 0:
                {
                  char *str1 = NULL; 

                  str2 = xstrndup(pos, end-pos);
                  if (strchr(">=!", str2[0])) {
                    str1 = strtok(str2, " ");
                    sign = str1[0];
                    alias = str1 + 1;
                    str1 += strlen(alias) +2;
                  } else str1 = str2;
                  if (str1 && strchr("$@*",str1[0])) {
                    op_pref = str1[0];
                    cmd = str1 + 1;
                  } else error_exit("Bad Line %d", line); 
                  goto found_device;
                }
            }
            pos = end2;
          }
end_line:
          // Did everything parse happily?
          if (field && field != 3) error_exit("bad line %d", line);
          // Next line
          pos = ++end;
        }
found_device:
        munmap(conf, len);
      }
      close(fd);
    }
  }

  if (operation && !((op_pref == '@' && !strcmp(operation, "add")) ||
        (op_pref == '$' && !strcmp(operation, "remove")) || (op_pref == '*')))
    cmd = NULL;

  if (alias) {
    if (alias[strlen(alias)-1] == '/') sprintf(temp,"%s%s",alias, device_name);
    else sprintf(temp,"%s",alias);
  }
  if (sign != '!' && operation && !strcmp(operation, "add")) { //not to create device otherwise

    if (mknod(device_name, mode | type, makedev(major, minor)) && errno != EEXIST)
      perror_msg("mknod /dev/%s failed", device_name);
    if (CFG_MDEV_CONF)  { 
      chmod(device_name, mode);
      chown(device_name, uid, gid);
    }

    if (TT.root_maj == major && TT.root_min == minor) symlink(device_name, "root");

    if (alias && (sign == '>' || sign == '=')) {
      mkpathat(AT_FDCWD, alias, 0, 2);
      if (rename(device_name, temp)) perror_exit("rename temp");
      if (sign == '>') symlink(temp, device_name);
    }
  }
  if (cmd) {
    char *str = xmprintf("%s=%s", "MDEV", device_name);

    putenv(str);
    if (system(cmd) == -1) perror_msg("can't run '%s'", cmd);
    unsetenv(str);
    free(str);
  }

  if (operation && !strcmp(operation, "remove") && major >= -1) {
    if (alias && sign == '>') unlink(temp);
    unlink(device_name);
  }
  free(str2);
  free(ln);
}

static int callback(struct dirtree *node)
{
  // Entries in /sys/class/block aren't char devices, so skip 'em.  (We'll
  // get block devices out of /sys/block.)
  if(!strcmp(node->name, "block")) return 0;
  if (!dirtree_notdotdot(node)) return 0;

  // Does this directory have a "dev" entry in it?
  // This is path based because the hotplug callbacks are
  if (S_ISDIR(node->st.st_mode) || S_ISLNK(node->st.st_mode)) {
    int len = 4;
    char *dev = dirtree_path(node, &len);
    strcpy(dev+len, "/dev");
    if (!access(dev, R_OK)) make_device(dev, "add");
    free(dev);
  }

  // Circa 2.6.25 the entries more than 2 deep are all either redundant
  // (mouse#, event#) or unnamed (every usb_* entry is called "device").

  return (node->parent && node->parent->parent) ? 0 :
    DIRTREE_RECURSE|DIRTREE_SYMFOLLOW;
}

#if 0
static int sequence_file(char *seq)
{
  static struct timespec tspec = { 0, 32*1000*1000 }; // time out after 2 secs
  int time_out = 2000 / 32, fd = -1;
  sigset_t sg;

  sigemptyset(&sg);
  sigaddset(&sg, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sg, NULL);

  for (;;) {
    ssize_t slen;
    char buf[sizeof(int)*3 + 2];

    if (fd < 0 && (fd = open("/dev/mdev.seq", O_RDWR)) < 0) break;
    if ((slen = pread(fd, buf, sizeof(buf) - 1, 0)) < 0) {
      close(fd);
      fd = -1;
      break;
    }
    buf[slen] = '\0';
    if (buf[0] == '\n') {
      xwrite(fd, seq, strlen(seq));
      xlseek(fd, 0, SEEK_SET);
      break;
    }
    if (!strcmp(seq, buf)) break;
    if (sigtimedwait(&sg, NULL, &tspec) >= 0) continue;
    if (!--time_out) break;
  }
  sigprocmask(SIG_UNBLOCK, &sg, NULL);
  return fd;
}
#endif

static void firmware_load(char *fware, char *sysfs_path)
{
  int count, fd, lfd, dfd;

  xchdir("/lib/firmware");
  fd = open(fware, O_RDONLY);
  // check for /sys/$DEVPATH/loading ... give 30 seconds to appear
  xchdir(sysfs_path);
  for (count = 0; count < 30; ++count) {
    lfd = open("loading", O_WRONLY);
    if (lfd >= 0) goto load;
    sleep(1);
  }
  goto end;

load:
  if (fd >= 0) {
    if (writeall(lfd, "1", 1) != 1) goto end;
    dfd = open("data", O_WRONLY);
    if (dfd < 0) goto end;
    xsendfile(fd, dfd);
    close(dfd);
  }
  if (fd >= 0) writeall(lfd, "0", 1);
  else writeall(lfd, "-1", 2);

end:
  xchdir("/dev");
  close(lfd);
  close(fd);
}

void mdev_main(void)
{

  char buf[PATH_MAX];
  struct dirtree *root;

  umask(0);
  xchdir("/dev");
  if (toys.optflags & FLAG_s) {
    struct stat st;

    xstat("/", &st);
    TT.root_maj = major(st.st_dev);
    TT.root_min = minor(st.st_dev);
    putenv((char*)"ACTION=add");

    root = dirtree_add_node(0, "/sys/class", 1);
    if (root) dirtree_handle_callback(root, callback);

    root = dirtree_add_node(0, "/sys/block", 1);
    if (root) dirtree_handle_callback(root, callback);
  } else {  // Hotplug handling
    char *fware, *action, *devpath;
    int logfd;

    action = getenv("ACTION");
    TT.devname = getenv("DEVNAME");
    devpath = getenv("DEVPATH");

    if (!action || !devpath) {
      toys.exithelp++;
      error_exit("env var action/devpath not found");
    }
    fware = getenv("FIRMWARE");

    if ((logfd = open("/dev/mdev.log", O_WRONLY | O_APPEND)) >= 0) {
      dup2(logfd, 2);  // 2 -> STDERR_FILENO
      TT.verbose = 1;
    }
    //TODO give sequencei file support.

    //    seqfd = seq_num ? sequence_file(seq_num) : -1;
    snprintf(buf, PATH_MAX, "/sys%s/dev", devpath);
    if (action && !strcmp("remove", action) && !fware)
      make_device(buf, action);
    else {
      make_device(buf, action);
      buf[strlen(buf) - 4] = '\0'; //remove /dev from end.
      if (action && !strcmp("add", action) && fware)
        firmware_load(fware, buf);
    }
  }
}
