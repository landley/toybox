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

#include "toys.h"

// mknod in /dev based on a path like "/sys/block/hda/hda1"
static void make_device(char *path)
{
  char *device_name = NULL, *s, *temp;
  int major, minor, type, len, fd;
  int mode = 0660;
  uid_t uid = 0;
  gid_t gid = 0;

  if (path) {
    // Try to read major/minor string

    temp = strrchr(path, '/');
    fd = open(path, O_RDONLY);
    *temp=0;
    temp = toybuf;
    len = read(fd, temp, 64);
    close(fd);
    if (len<1) return;
    temp[len] = 0;

    // Determine device type, major and minor

    type = path[5]=='c' ? S_IFCHR : S_IFBLK;
    major = minor = 0;
    sscanf(temp, "%u:%u", &major, &minor);
  } else {
    // if (!path), do hotplug

    if (!(temp = getenv("SUBSYSTEM")))
      return;
    type = strcmp(temp, "block") ? S_IFCHR : S_IFBLK;
    major = minor = 0;
    if (!(temp = getenv("MAJOR")))
      return;
    sscanf(temp, "%u", &major);
    if (!(temp = getenv("MINOR")))
      return;
    sscanf(temp, "%u", &minor);
    path = getenv("DEVPATH");
    device_name = getenv("DEVNAME");
    if (!path)
      return;
    temp = toybuf;
  }
  if (!device_name)
    device_name = strrchr(path, '/') + 1;

  // If we have a config file, look up permissions for this device

  if (CFG_MDEV_CONF) {
    char *conf, *pos, *end;

    // mmap the config file
    if (-1!=(fd = open("/etc/mdev.conf", O_RDONLY))) {
      len = fdlength(fd);
      conf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
      if (conf) {
        int line = 0;

        // Loop through lines in mmaped file
        for (pos = conf; pos-conf<len;) {
          int field;
          char *end2;

          line++;
          // find end of this line
          for(end = pos; end-conf<len && *end!='\n'; end++);

          // Three fields: regex, uid:gid, mode
          for (field = 3; field; field--) {
            // Skip whitespace
            while (pos<end && isspace(*pos)) pos++;
            if (pos==end || *pos=='#') break;
            for (end2 = pos;
              end2<end && !isspace(*end2) && *end2!='#'; end2++);
            switch(field) {
              // Regex to match this device
              case 3:
              {
                char *regex = strndup(pos, end2-pos);
                regex_t match;
                regmatch_t off;
                int result;

                // Is this it?
                xregcomp(&match, regex, REG_EXTENDED);
                result=regexec(&match, device_name, 1, &off, 0);
                regfree(&match);
                free(regex);

                // If not this device, skip rest of line
                if (result || off.rm_so
                  || off.rm_eo!=strlen(device_name))
                    goto end_line;

                break;
              }
              // uid:gid
              case 2:
              {
                char *s2;

                // Find :
                for(s = pos; s<end2 && *s!=':'; s++);
                if (s==end2) goto end_line;

                // Parse UID
                uid = strtoul(pos,&s2,10);
                if (s!=s2) {
                  struct passwd *pass;
                  char *str = strndup(pos, s-pos);
                  pass = getpwnam(str);
                  free(str);
                  if (!pass) goto end_line;
                  uid = pass->pw_uid;
                }
                s++;
                // parse GID
                gid = strtoul(s,&s2,10);
                if (end2!=s2) {
                  struct group *grp;
                  char *str = strndup(s, end2-s);
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
                if (pos!=end2) goto end_line;
                goto found_device;
              }
            }
            pos=end2;
          }
end_line:
          // Did everything parse happily?
          if (field && field!=3) error_exit("Bad line %d", line);

          // Next line
          pos = ++end;
        }
found_device:
        munmap(conf, len);
      }
      close(fd);
    }
  }

  sprintf(temp, "/dev/%s", device_name);

  if (getenv("ACTION") && !strcmp(getenv("ACTION"), "remove")) {
    unlink(temp);
    return;
  }

  if (strchr(device_name, '/'))
    mkpathat(AT_FDCWD, temp, 0, 2);
  if (mknod(temp, mode | type, makedev(major, minor)) && errno != EEXIST)
    perror_exit("mknod %s failed", temp);

 
  if (type == S_IFBLK) close(open(temp, O_RDONLY)); // scan for partitions

  if (CFG_MDEV_CONF) mode=chown(temp, uid, gid);
}

static int callback(struct dirtree *node)
{
  // Entries in /sys/class/block aren't char devices, so skip 'em.  (We'll
  // get block devices out of /sys/block.)
  if(!strcmp(node->name, "block")) return 0;

  // Does this directory have a "dev" entry in it?
  // This is path based because the hotplug callbacks are
  if (S_ISDIR(node->st.st_mode) || S_ISLNK(node->st.st_mode)) {
    int len=4;
    char *dev = dirtree_path(node, &len);
    strcpy(dev+len, "/dev");
    if (!access(dev, R_OK)) make_device(dev);
    free(dev);
  }

  // Circa 2.6.25 the entries more than 2 deep are all either redundant
  // (mouse#, event#) or unnamed (every usb_* entry is called "device").

  return (node->parent && node->parent->parent) ? 0 : DIRTREE_RECURSE;
}

void mdev_main(void)
{
  // Handle -s

  if (toys.optflags) {
    dirtree_read("/sys/class", callback);
    dirtree_read("/sys/block", callback);
  } else { // hotplug support
    make_device(NULL);
  }
}
