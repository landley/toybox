/* wget.c - get the content file in HTTP server
 *
 * Copyright 2016 Lipi C.H. Lee <lipisoft@gmail.com>
 *

USE_WGET(NEWTOY(wget, "f", TOYFLAG_USR|TOYFLAG_BIN))

config WGET
  bool "wget"
  default n
  help
    usage: wget [-f FILENAME] URL

    Get the content file in HTTP server

*/

#define FOR_wget
#include "toys.h"

GLOBALS(
  int unused;
)

void wget_main(void)
{
  int sock_fd;
  struct addrinfo hints, *result, *rp;
  char get[] = "GET / HTTP/1.1\r\n"
               "\r\n";
  char user_agent[] = "User-Agent: toybox wget/0.6.1\r\n"
  char connection_type[] = "Connection: close\r\n"
  char host[] = "Host: localhost\r\n"
  char response[11];
  unsigned int len;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if ((errno = getaddrinfo("localhost", "80", &hints, &result)))
    perror_exit("getaddrinfo: %s", gai_strerror(errno));

  // try all address list(IPv4 or IPv6) until succeed
  for (rp = result; rp; rp = rp->ai_next) {
    if ((sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol))
        == -1)
      continue;
    if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;
    close(sock_fd);
  }

  if(!rp) perror_exit("Could not connect");

  len = strlen(get);
  if (write(sock_fd, get, len) != len)
    perror_exit("write failed.");

  while ((len = read(sock_fd, response, 10)) > 0) {
    response[len] = '\0';
    xprintf("%s", response);
  }

  close(sock_fd);
  xprintf("\n");
  freeaddrinfo(result);
}
