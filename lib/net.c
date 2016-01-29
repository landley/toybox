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

int xconnect(char *host, char *port, int family, int socktype, int protocol,
             int flags)
{
  struct addrinfo info, *ai, *ai2;
  int fd;

  memset(&info, 0, sizeof(struct addrinfo));
  info.ai_family = family;
  info.ai_socktype = socktype;
  info.ai_protocol = protocol;
  info.ai_flags = flags;

  fd = getaddrinfo(host, port, &info, &ai);
  if (fd || !ai)
    error_exit("Connect '%s%s%s': %s", host, port ? ":" : "", port ? port : "",
      fd ? gai_strerror(fd) : "not found");

  // Try all the returned addresses. Report errors if last entry can't connect.
  for (ai2 = ai; ai; ai = ai->ai_next) {
    fd = (ai->ai_next ? socket : xsocket)(ai->ai_family, ai->ai_socktype,
      ai->ai_protocol);
    if (!connect(fd, ai->ai_addr, ai->ai_addrlen)) break;
    else if (!ai2->ai_next) perror_exit("connect");
    close(fd);
  }
  freeaddrinfo(ai2);

  return fd;
}

int xpoll(struct pollfd *fds, int nfds, int timeout)
{
  int i;

  for (;;) {
    if (0>(i = poll(fds, nfds, timeout))) {
      if (toys.signal) return i;
      if (errno != EINTR && errno != ENOMEM) perror_exit("xpoll");
      else if (timeout>0) timeout--;
    } else return i;
  }
}
