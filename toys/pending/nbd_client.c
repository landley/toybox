/* vi: set sw=4 ts=4:
 *
 * nbd-client.c - network block device client
 *
 * Copyright 2010 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.

USE_NBD_CLIENT(NEWTOY(nbd_client, "<3>3", TOYFLAG_USR|TOYFLAG_BIN))

config NBD_CLIENT
  bool "nbd-client"
  default n
  help
    Usage: nbd-client [-sSpn] [-b BLKSZ] [-t SECS] [-N name] HOST PORT DEVICE

    -b	block size
    -t	timeout in seconds
    -s	swap
    -S	sdp
    -p	persist
    -n	nofork
    -d	DEVICE
    -c	DEVICE
*/

#define FOR_nbd_client
#include "toys.h"

//#include <errno.h>
//#include <fcntl.h>
//#include <limits.h>
#include <netdb.h>
//#include <stdint.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <unistd.h>
#include <netinet/tcp.h>
//#include <sys/ioctl.h>
//#include <sys/mount.h>
//#include <sys/types.h>
//#include <sys/socket.h>
//#include <sys/stat.h>

#define NBD_SET_SOCK          _IO(0xab, 0)
#define NBD_SET_BLKSIZE       _IO(0xab, 1)
#define NBD_SET_SIZE          _IO(0xab, 2)
#define NBD_DO_IT             _IO(0xab, 3)
#define NBD_CLEAR_SOCK        _IO(0xab, 4)
#define NBD_CLEAR_QUEUE       _IO(0xab, 5)
#define NBD_PRINT_DEBUG       _IO(0xab, 6)
#define NBD_SET_SIZE_BLOCKS   _IO(0xab, 7)
#define NBD_DISCONNECT        _IO(0xab, 8)
#define NBD_SET_TIMEOUT       _IO(0xab, 9)

void nbd_client_main(void)
{
  int sock = -1, nbd, flags;
  unsigned long timeout = 0;
  struct addrinfo *addr, *p;
  char *host=toys.optargs[0], *port=toys.optargs[1], *device=toys.optargs[2];
  uint64_t devsize;
  char data[124];

  // Parse command line stuff (just a stub now)

  // Make sure the /dev/nbd exists.

  if (0>(nbd = open(device, O_RDWR))) {
    fprintf(stderr, "Can't open '%s'\n", device);
    exit(1);
  }

  // Repeat until spanked

  for (;;) {
    int temp;
    struct addrinfo hints;

    // Find and connect to server

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addr)) addr = 0;
    for (p = addr; p; p = p->ai_next) {
      sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (-1 != connect(sock, p->ai_addr, p->ai_addrlen)) break;
    }
    freeaddrinfo(addr);

    if (!p) {
      fprintf(stderr, "Can't connect '%s' port '%s'\n", host, port);
      exit(1);
    }

    temp = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &temp, sizeof(int));

    // Log on to the server.  (Todo: one big 8+8+8+4+124=152 read)

    if (read(sock, data, 8) != 8 || memcmp(data, "NBDMAGIC", 8)
      || read(sock, &devsize, 8) != 8
      || SWAP_BE64(devsize) != 0x420281861253LL
      || read(sock, &devsize, 8) != 8 || read(sock, &flags, 4) != 4
      || read(sock, data, 124) != 124)
    {
      fprintf(stderr, "Login fail\n");
      exit(1);
    }
    devsize = SWAP_BE64(devsize);
    flags = SWAP_BE32(flags);

    // Set 4k block size.  Everything uses that these days.
    ioctl(nbd, NBD_SET_BLKSIZE, 4096);
    ioctl(nbd, NBD_SET_SIZE_BLOCKS, devsize/4096);
    ioctl(nbd, NBD_CLEAR_SOCK);

    // If the sucker was exported read only, respect that locally.
    temp = (flags & 2) ? 1 : 0;
    if (ioctl(nbd, BLKROSET, &temp)<0) {
      fprintf(stderr, "Login fail\n");
      exit(1);
    }

    if (timeout && ioctl(nbd, NBD_SET_TIMEOUT, timeout)<0) break;
    if (ioctl(nbd, NBD_SET_SOCK, sock) < 0) break;

    // if (swap) mlockall(MCL_CURRENT|MCL_FUTURE);

    // Open the device to force reread of the partition table.
    if (!fork()) {
      char *s = strrchr(device, '/');
      sprintf(data, "/sys/block/%.32s/pid", s ? s+1 : device);
      // Is it up yet?
      for (;;) {
        temp = open(data, O_RDONLY);
        if (temp == -1) sleep(1);
        else {
          close(temp);
          break;
        }
      }
      close(open(device, O_RDONLY));
      exit(0);
    }

    // Daemonize here.

    daemon(0,0);

    // Process NBD requests until further notice.

    if (ioctl(nbd, NBD_DO_IT)>=0 || errno==EBADR) break;
    close(sock);
    close(nbd);
  }

  // Flush queue and exit.

  ioctl(nbd, NBD_CLEAR_QUEUE);
  ioctl(nbd, NBD_CLEAR_SOCK);

  exit(0);
}
