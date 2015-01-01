/* losetup.c - Loopback setup
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * No standard. (Sigh.)

USE_LOSETUP(NEWTOY(losetup, ">2S(sizelimit)#s(show)ro#j:fdca[!afj]", TOYFLAG_SBIN))

config LOSETUP
  bool "losetup"
  default y
  help
    usage: losetup [-cdrs] [-o OFFSET] [-S SIZE] {-d DEVICE...|-j FILE|-af|{DEVICE FILE}}

    Associate a loopback device with a file, or show current file (if any)
    associated with a loop device.

    Instead of a device:
    -a	Iterate through all loopback devices
    -f	Find first unused loop device (may create one)
    -j	Iterate through all loopback devices associated with FILE

    existing:
    -c	Check capacity (file size changed)
    -d	Detach loopback device

    new:
    -s	Show device name (alias --show)
    -o	Start assocation at OFFSET into FILE
    -r	Read only
    -S	Limit SIZE of loopback association (alias --sizelimit)
*/

#define FOR_losetup
#include "toys.h"
#include <linux/loop.h>

GLOBALS(
  char *jfile;
  long offset;
  long size;

  int openflags;
  dev_t jdev;
  ino_t jino;
)

/*
todo: basic /dev file association
  associate DEV FILE
  #-a
  cdfjosS
  allocate new loop device:
    /dev/loop-control
    https://lkml.org/lkml/2011/7/26/148
*/

// -f: *device is NULL

// Perform requested operation on one device. Returns 1 if handled, 0 if error
static void loopback_setup(char *device, char *file)
{
  struct loop_info64 *loop = (void *)(toybuf+32);
  int lfd = -1, ffd = ffd;
  unsigned flags = toys.optflags;

  // Open file (ffd) and loop device (lfd)

  if (file) ffd = xopen(file, TT.openflags);
  if (!device) {
    int i, cfd = open("/dev/loop-control", O_RDWR);

    // We assume /dev is devtmpfs so device creation has no lag. Otherwise
    // just preallocate loop devices and stay within them.

    // mount -o loop depends on found device being at the start of toybuf.
    if (cfd != -1) {
      if (0 <= (i = ioctl(cfd, 0x4C82))) // LOOP_CTL_GET_FREE
        sprintf(device = toybuf, "/dev/loop%d", i);
      close(cfd);
    }
  }

  if (device) lfd = open(device, TT.openflags);

  // Stat the loop device to see if there's a current association.
  memset(loop, 0, sizeof(struct loop_info64));
  if (-1 == lfd || ioctl(lfd, LOOP_GET_STATUS64, loop)) {
    if (errno == ENXIO && (flags & (FLAG_a|FLAG_j))) goto done;
    if (errno != ENXIO || !file) {
      perror_msg("%s", device ? device : "-f");
      goto done;
    }
  }

  // Skip -j filtered devices
  if (TT.jfile && (loop->lo_device != TT.jdev || loop->lo_inode != TT.jino))
    goto done;

  // Check size of file or delete existing association
  if (flags & (FLAG_c|FLAG_d)) {
    // The constant is LOOP_SET_CAPACITY
    if (ioctl(lfd, (flags & FLAG_c) ? 0x4C07 : LOOP_CLR_FD, 0)) {
      perror_msg("%s", device);
      goto done;
    }
  // Associate file with this device?
  } else if (file) {
    char *s = xabspath(file, 1);

    if (!s) perror_exit("file"); // already opened, but if deleted since...
    if (ioctl(lfd, LOOP_SET_FD, ffd)) perror_exit("%s=%s", device, file);
    loop->lo_offset = TT.offset;
    loop->lo_sizelimit = TT.size;
    xstrncpy((char *)loop->lo_file_name, s, LO_NAME_SIZE);
    s[LO_NAME_SIZE-1] = 0;
    if (ioctl(lfd, LOOP_SET_STATUS64, loop)) perror_exit("%s=%s", device, file);
    if (flags & FLAG_s) printf("%s", device);
    free(s);
  } else if (flags & FLAG_f) printf("%s", device);
  else {
    xprintf("%s: [%04llx]:%llu (%s)", device, loop->lo_device, loop->lo_inode,
      loop->lo_file_name);
    if (loop->lo_offset) xprintf(", offset %llu", loop->lo_offset);
    if (loop->lo_sizelimit) xprintf(", sizelimit %llu", loop->lo_sizelimit);
    xputc('\n');
  }

done:
  if (file) close(ffd);
  if (lfd != -1) close(lfd);
}

// Perform an action on all currently existing loop devices
static int dash_a(struct dirtree *node)
{
  char *s = node->name;

  // Initial /dev node needs to recurse down one level, then only loop[0-9]*
  if (*s == '/') return DIRTREE_RECURSE;
  if (strncmp(s, "loop", 4) || !isdigit(s[4])) return 0;

  s = dirtree_path(node, 0);
  loopback_setup(s, 0);
  free(s);

  return 0;
}

void losetup_main(void)
{
  char **s;

  TT.openflags = (toys.optflags & FLAG_r) ? O_RDONLY : O_RDWR;

  if (TT.jfile) {
    struct stat st;

    xstat(TT.jfile, &st);
    TT.jdev = st.st_dev;
    TT.jino = st.st_ino;
  }

  // With just device, display current association
  // -a, -f substitute for device
  // -j substitute for device

  // new association: S size o offset rs - need a file
  // existing association: cd

  // -f(dc FILE)

  if (toys.optflags & FLAG_f) {
    if (toys.optc > 1) perror_exit("max 1 arg");
    loopback_setup(NULL, *toys.optargs);
  } else if (toys.optflags & (FLAG_a|FLAG_j)) {
    if (toys.optc) error_exit("bad args");
    dirtree_read("/dev", dash_a);
  // Do we need one DEVICE argument?
  } else {
    char *file = (toys.optflags & (FLAG_d|FLAG_c)) ? NULL : toys.optargs[1];

    if (!toys.optc || (file && toys.optc != 2)) {
      toys.exithelp++;
      perror_exit("needs %d arg%s", 1+!!file, file ? "s" : "");
    }
    for (s = toys.optargs; *s; s++) {
      loopback_setup(*s, file);
      if (file) break;
    }
  }
}
