/* vi:set ts=4:
 *
 * mdev - Mini udev for busybox
 *
 * Copyright 2005, 2008 Rob Landley <rob@landley.net>
 * Copyright 2005 Frank Sorenson <frank@tuxrocks.com>
 *
 * Not in SUSv3.

USE_MDEV(NEWTOY(mdev, "s", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_UMASK))

config MDEV
	bool "mdev"
	default y
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

	  Each line must contain three whitespace separated fields.  The first
	  field is a regular expression matching one or more device names, and
	  the second and third fields are uid:gid and file permissions for
	  matching devies.
*/

#include "toys.h"
#include "lib/xregcomp.h"

// mknod in /dev based on a path like "/sys/block/hda/hda1"
static void make_device(char *path)
{
	char *device_name, *s, *temp;
	int major, minor, type, len, fd;
	int mode = 0660;
	uid_t uid = 0;
	gid_t gid = 0;

	// Try to read major/minor string

	temp = strrchr(path, '/');
	fd = open(path, O_RDONLY);
	*temp=0;
	temp++;
	len = read(fd, temp, 64);
	close(fd);
	if (len<1) return;
	temp[len] = 0;

	// Determine device name, type, major and minor

	device_name = strrchr(path, '/') + 1;
	type = path[5]=='c' ? S_IFCHR : S_IFBLK;
	major = minor = 0;
	sscanf(temp, "%u:%u", &major, &minor);

	// If we have a config file, look up permissions for this device

	if (CFG_MDEV_CONF) {
		char *conf, *pos, *end;

		// mmap the config file
		if (-1!=(fd = open("/etc/mdev.conf", O_RDONLY))) {
			len = lseek(fd, 0, SEEK_END);
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
								char *regex = strndupa(pos, end2-pos);
								regex_t match;
								regmatch_t off;
								int result;

								// Is this it?
								xregcomp(&match, regex, REG_EXTENDED);
								result=regexec(&match, device_name, 1, &off, 0);
								regfree(&match);

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
									pass = getpwnam(strndupa(pos, s-pos));
									if (!pass) goto end_line;
									uid = pass->pw_uid;
								}
								s++;
								// parse GID
								gid = strtoul(s,&s2,10);
								if (end2!=s2) {
									struct group *grp;
									grp = getgrnam(strndupa(s, end2-s));
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
	if (mknod(temp, mode | type, makedev(major, minor)) && errno != EEXIST)
		perror_exit("mknod %s failed", temp);

	// Dear gcc: shut up about ignoring the return value here.  If it doesn't
	// work, what exactly are we supposed to do about it?
	if (CFG_MDEV_CONF) mode=chown(temp, uid, gid);
}

static int callback(char *path, struct dirtree *node)
{
	// Entries in /sys/class/block aren't char devices, so skip 'em.  (We'll
	// get block devices out of /sys/block.)
	if(!strcmp(node->name, "block")) return 1;

	// Does this directory have a "dev" entry in it?
	if (S_ISDIR(node->st.st_mode) || S_ISLNK(node->st.st_mode)) {
		char *dest = path+strlen(path);
		strcpy(dest, "/dev");
		if (!access(path, R_OK)) make_device(path);
		*dest = 0;
	}

	// Circa 2.6.25 the entries more than 2 deep are all either redundant
	// (mouse#, event#) or unnamed (every usb_* entry is called "device").
	return node->depth>1;
}

void mdev_main(void)
{
	// Handle -s

	if (toys.optflags) {
		xchdir("/sys/class");
		strcpy(toybuf, "/sys/class");
		dirtree_read(toybuf, NULL, callback);
		strcpy(toybuf+5, "block");
		dirtree_read(toybuf, NULL, callback);
	}
//	if (toys.optflags) {
//		strcpy(toybuf, "/sys/block");
//		find_dev(toybuf);
//		strcpy(toybuf, "/sys/class");
//		find_dev(toybuf);
//		return;
//	}

	// hotplug support goes here
}
