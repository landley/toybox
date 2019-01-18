/* sntp.c - sntp client and server
 *
 * Copyright 2019 Rob Landley <rob@landley.net>
 *
 * See https://www.ietf.org/rfc/rfc4330.txt

USE_SNTP(NEWTOY(sntp, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config SNTP
  bool "sntp"
  default n
  help
    usage: sntp SERVER...

    Simple Network Time Protocol client, set system clock from a server.
*/

#define FOR_sntp
#include "toys.h"

GLOBALS(
  int unused;
)

union socksaddr {
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
};

// timeout in milliseconds
int xrecvwait(int fd, char *buf, int len, union socksaddr *sa, int timeout)
{
  socklen_t sl = sizeof(*sa);

  if (timeout >= 0) {
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    if (!xpoll(&pfd, 1, timeout)) return 0;
  }

  len = recvfrom(fd, buf, len, 0, (void *)sa, &sl);
  if (len<0) perror_exit("recvfrom");

  return len;
}

void sntp_main(void)
{
  struct addrinfo *ai;
  union socksaddr sa;
  int fd, len;

  ai = xgetaddrinfo(*toys.optargs, 0, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP, 0);
  // When root, bind to local server address
  if (!getuid())
    fd = xbind(xgetaddrinfo("", "123", AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP, 0));
  else fd = xsocket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);

  xsendto(fd, toybuf, 48, ai->ai_addr);
  len = xrecvwait(fd, toybuf, sizeof(toybuf), &sa, 5000);
  printf("%d\n", len);
  if (len>0) write(1, toybuf, len);
}
