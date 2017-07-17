/* nbd-client.c - network block device client
 *
 * Copyright 2010 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.

// This little dance is because a NEWTOY with - in the name tries to do
// things like prototype "nbd-client_main" which isn't a valid symbol. So
// we hide the underscore name and OLDTOY the name we want.
USE_NBD_CLIENT(NEWTOY(nbd_client, "<3>3ns", 0))
USE_NBD_CLIENT(OLDTOY(nbd-client, nbd_client, TOYFLAG_USR|TOYFLAG_BIN))

config NBD_CLIENT
  bool "nbd-client"
  depends on TOYBOX_FORK
  default y
  help
    usage: nbd-client [-ns] HOST PORT DEVICE

    -n	Do not fork into background
    -s	nbd swap support (lock server into memory)
*/

/*  TODO:
    usage: nbd-client [-sSpn] [-b BLKSZ] [-t SECS] [-N name] HOST PORT DEVICE

    -b	block size
    -t	timeout in seconds
    -S	sdp
    -p	persist
    -n	nofork
    -d	DEVICE
    -c	DEVICE
*/

#define FOR_nbd_client
#include "toys.h"
#include <linux/nbd.h>

void nbd_client_main(void)
{
  int sock = -1, nbd, flags;
  unsigned long timeout = 0;
  char *host=toys.optargs[0], *port=toys.optargs[1], *device=toys.optargs[2];
  uint64_t devsize;

  // Repeat until spanked

  nbd = xopen(device, O_RDWR);
  for (;;) {
    int temp;

    // Find and connect to server

    sock = xconnect(xgetaddrinfo(host, port, AF_UNSPEC, SOCK_STREAM, 0, 0));
    temp = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &temp, sizeof(int));

    // Read login data

    xreadall(sock, toybuf, 152);
    if (memcmp(toybuf, "NBDMAGIC\x00\x00\x42\x02\x81\x86\x12\x53", 16))
      error_exit("bad login %s:%s", host, port);
    devsize = SWAP_BE64(*(uint64_t *)(toybuf+16));
    flags = SWAP_BE32(*(int *)(toybuf+24));

    // Set 4k block size.  Everything uses that these days.
    ioctl(nbd, NBD_SET_BLKSIZE, 4096);
    ioctl(nbd, NBD_SET_SIZE_BLOCKS, devsize/4096);
    ioctl(nbd, NBD_CLEAR_SOCK);

    // If the sucker was exported read only, respect that locally.
    temp = (flags & 2) ? 1 : 0;
    xioctl(nbd, BLKROSET, &temp);

    if (timeout && ioctl(nbd, NBD_SET_TIMEOUT, timeout)<0) break;
    if (ioctl(nbd, NBD_SET_SOCK, sock) < 0) break;

    if (toys.optflags & FLAG_s) mlockall(MCL_CURRENT|MCL_FUTURE);

    // Open the device to force reread of the partition table.
    if ((toys.optflags & FLAG_n) || !xfork()) {
      char *s = strrchr(device, '/');
      int i;

      sprintf(toybuf, "/sys/block/%.32s/pid", s ? s+1 : device);
      // Is it up yet? (Give it 10 seconds.)
      for (i=0; i<100; i++) {
        temp = open(toybuf, O_RDONLY);
        if (temp == -1) msleep(100);
        else {
          close(temp);
          break;
        }
      }
      close(open(device, O_RDONLY));
      if (!(toys.optflags & FLAG_n)) exit(0);
    }

    // Daemonize here.

    if (daemon(0,0)) perror_exit("daemonize");

    // Process NBD requests until further notice.

    if (ioctl(nbd, NBD_DO_IT)>=0 || errno==EBADR) break;
    close(sock);
  }

  // Flush queue and exit.
  ioctl(nbd, NBD_CLEAR_QUE);
  ioctl(nbd, NBD_CLEAR_SOCK);
  if (CFG_TOYBOX_FREE) close(nbd);
}
