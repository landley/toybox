#include "toys.h"

int xsocket(int domain, int type, int protocol)
{
  int fd = socket(domain, type, protocol);

  if (fd < 0) perror_exit("socket %x %x", type, protocol);
  fcntl(fd, F_SETFD, FD_CLOEXEC);

  return fd;
}

void xsetsockopt(int fd, int level, int opt, void *val, socklen_t len)
{
  if (-1 == setsockopt(fd, level, opt, val, len)) perror_exit("setsockopt");
}

struct addrinfo *xgetaddrinfo(char *host, char *port, int family, int socktype,
  int protocol, int flags)
{
  struct addrinfo info, *ai;
  int rc;

  memset(&info, 0, sizeof(struct addrinfo));
  info.ai_family = family;
  info.ai_socktype = socktype;
  info.ai_protocol = protocol;
  info.ai_flags = flags;

  rc = getaddrinfo(host, port, &info, &ai);
  if (rc || !ai)
    error_exit("%s%s%s: %s", host, port ? ":" : "", port ? port : "",
      rc ? gai_strerror(rc) : "not found");

  return ai;
}

int xconnect(struct addrinfo *ai_arg)
{
  struct addrinfo *ai;
  int fd = -1;

  // Try all the returned addresses. Report errors if last entry can't connect.
  for (ai = ai_arg; ai; ai = ai->ai_next) {
    fd = (ai->ai_next ? socket : xsocket)(ai->ai_family, ai->ai_socktype,
      ai->ai_protocol);
    if (!connect(fd, ai->ai_addr, ai->ai_addrlen)) break;
    else if (!ai->ai_next) perror_exit("connect");
    close(fd);
  }
  freeaddrinfo(ai_arg);

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

// Loop forwarding data from in1 to out1 and in2 to out2, handling
// half-connection shutdown. timeouts return if no data for X miliseconds.
// Returns 0: both closed, 1 shutdown_timeout, 2 timeout
int pollinate(int in1, int in2, int out1, int out2, int timeout, int shutdown_timeout)
{
  struct pollfd pollfds[2];
  int i, pollcount = 2;

  memset(pollfds, 0, 2*sizeof(struct pollfd));
  pollfds[0].events = pollfds[1].events = POLLIN;
  pollfds[0].fd = in1;
  pollfds[1].fd = in2;

  // Poll loop copying data from each fd to the other one.
  for (;;) {
    if (!xpoll(pollfds, pollcount, timeout)) return pollcount;

    for (i=0; i<pollcount; i++) {
      if (pollfds[i].revents & POLLIN) {
        int len = read(pollfds[i].fd, libbuf, sizeof(libbuf));
        if (len<1) pollfds[i].revents = POLLHUP;
        else xwrite(i ? out2 : out1, libbuf, len);
      }
      if (pollfds[i].revents & POLLHUP) {
        // Close half-connection.  This is needed for things like
        // "echo GET / | netcat landley.net 80"
        // Note that in1 closing triggers timeout, in2 returns now.
        if (i) {
          shutdown(pollfds[0].fd, SHUT_WR);
          pollcount--;
          timeout = shutdown_timeout;
        } else return 0;
      }
    }
  }
}

// Return converted numeric address in libbuf
char *ntop(struct sockaddr *sa)
{
  void *addr;

  if (sa->sa_family == AF_INET) addr = &((struct sockaddr_in *)sa)->sin_addr;
  else addr = &((struct sockaddr_in6 *)sa)->sin6_addr;

  inet_ntop(sa->sa_family, addr, libbuf, sizeof(libbuf));

  return libbuf;
}
