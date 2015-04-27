#include "toys.h"

int xsocket(int domain, int type, int protocol)
{
  int fd = socket(domain, type, protocol);

  if (fd < 0) perror_exit("socket %x %x", type, protocol);
  return fd;
}

void xsetsockopt(int fd, int level, int opt, void *val, socklen_t len)
{
  if (-1 == setsockopt(fd, level, opt, val, len)) perror_exit("setsockopt");
}
