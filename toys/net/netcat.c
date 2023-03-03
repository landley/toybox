/* netcat.c - Forward stdin/stdout to a file or network connection.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * TODO: genericize for telnet/microcom/tail-f, fix -t

USE_NETCAT(NEWTOY(netcat, "^tElLw#<1W#<1p#<1>65535q#<1s:f:46uUn[!tlL][!Lw][!Lu][!46U]", TOYFLAG_BIN))
USE_NETCAT(OLDTOY(nc, netcat, TOYFLAG_USR|TOYFLAG_BIN))

config NETCAT
  bool "netcat"
  default y
  help
    usage: netcat [-46ElLtUu] [-wpq #] [-s addr] {IPADDR PORTNUM|-f FILENAME|COMMAND...}

    Forward stdin/stdout to a file or network connection.

    -4	Force IPv4
    -6	Force IPv6
    -E	Forward stderr
    -f	Use FILENAME (ala /dev/ttyS0) instead of network
    -l	Listen for one incoming connection, then exit
    -L	Listen and background each incoming connection (server mode)
    -n	No DNS lookup
    -p	Local port number
    -q	Quit SECONDS after EOF on stdin, even if stdout hasn't closed yet
    -s	Local source address
    -t	Allocate tty
    -u	Use UDP
    -U	Use a UNIX domain socket
    -w	SECONDS timeout to establish connection
    -W	SECONDS timeout for more data on an idle connection

    When listening the COMMAND line is executed as a child process to handle
    an incoming connection. With no COMMAND -l forwards the connection
    to stdin/stdout. If no -p specified, -l prints the port it bound to and
    backgrounds itself (returning immediately).

    For a quick-and-dirty server, try something like:
    netcat -s 127.0.0.1 -p 1234 -tL sh -l

    Or use "stty 115200 -F /dev/ttyS0 && stty raw -echo -ctlecho" with
    netcat -f to connect to a serial port.
*/

#define FOR_netcat
#include "toys.h"

GLOBALS(
  char *f, *s;
  long q, p, W, w;
)

static void timeout(int signum)
{
  if (TT.w) error_exit("Timeout");
  xexit();
}

// open AF_UNIX socket
static int usock(char *name, int type, int out)
{
  int sockfd;
  struct sockaddr_un sockaddr;

  memset(&sockaddr, 0, sizeof(struct sockaddr_un));

  if (strlen(name) + 1 > sizeof(sockaddr.sun_path))
    error_exit("socket path too long %s", name);
  strcpy(sockaddr.sun_path, name);
  sockaddr.sun_family = AF_UNIX;

  sockfd = xsocket(AF_UNIX, type, 0);
  (out?xconnect:xbind)(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));

  return sockfd;
}

void netcat_main(void)
{
  int sockfd = -1, in1 = 0, in2 = 0, out1 = 1, out2 = 1, family = AF_UNSPEC,
    type = FLAG(u) ? SOCK_DGRAM : SOCK_STREAM;
  socklen_t len;
  pid_t child;

  // Addjust idle and quit_delay to ms or -1 for no timeout
  TT.W = TT.W ? TT.W*1000 : -1;
  TT.q = TT.q ? TT.q*1000 : -1;

  xsignal(SIGCHLD, SIG_IGN);
  if (TT.w) {
    xsignal(SIGALRM, timeout);
    alarm(TT.w);
  }

  // The argument parsing logic can't make "<2" conditional on other
  // arguments like -f and -l, so do it by hand here.
  if (FLAG(f) ? toys.optc : (!FLAG(l) && !FLAG(L) && toys.optc!=2-FLAG(U)))
    help_exit("bad argument count");

  if (FLAG(4)) family = AF_INET;
  else if (FLAG(6)) family = AF_INET6;
  else if (FLAG(U)) family = AF_UNIX;

  if (TT.f) in1 = out2 = xopen(TT.f, O_RDWR);
  else {
    // Setup socket
    if (!FLAG(l) && !FLAG(L)) {
      if (FLAG(U)) sockfd = usock(toys.optargs[0], type, 1);
      else sockfd = xconnectany(xgetaddrinfo(toys.optargs[0],
        toys.optargs[1], family, type, 0, AI_NUMERICHOST*FLAG(n)));

      // We have a connection. Disarm timeout and start poll/send loop.
      alarm(0);
      in1 = out2 = sockfd;
      pollinate(in1, in2, out1, out2, TT.W, TT.q);
    } else {
      // Listen for incoming connections
      if (FLAG(U)) {
        if (!FLAG(s)) error_exit("-s must be provided if using -U with -L/-l");
        sockfd = usock(TT.s, type, 0);
      } else {
        sprintf(toybuf, "%ld", TT.p);
        sockfd = xbindany(xgetaddrinfo(TT.s, toybuf, family, type, 0, 0));
      }

      if (!FLAG(u) && listen(sockfd, 5)) perror_exit("listen");
      if (!TT.p && !FLAG(U)) {
        struct sockaddr* address = (void*)toybuf;
        short port_be;

        len = sizeof(struct sockaddr_storage);
        getsockname(sockfd, address, &len);
        if (address->sa_family == AF_INET)
          port_be = ((struct sockaddr_in*)address)->sin_port;
        else if (address->sa_family == AF_INET6)
          port_be = ((struct sockaddr_in6*)address)->sin6_port;
        else perror_exit("getsockname: bad family");

        dprintf(1, "%d\n", SWAP_BE16(port_be));
        // Return immediately if no -p and -Ll has arguments, so wrapper
        // script can use port number.
        if (CFG_TOYBOX_FORK && toys.optc && xfork()) goto cleanup;
      }

      do {
       len = sizeof(struct sockaddr_storage);
        if (FLAG(u)) {
          if (-1 == recvfrom(in1 = dup(sockfd), &child, 1, MSG_PEEK,
            (void *)toybuf, &len)) perror_exit("recvfrom");
        } else if ((in1 = accept(sockfd, 0, 0))<0) perror_exit("accept");
        out2 = in1;
        child = 0;

        // We have a connection. Disarm timeout.
        alarm(0);

        // Fork a child as necessary. Parent cleans up and continues here.
        if (toys.optc && FLAG(L)) NOEXIT(child = XVFORK());
        if (child) {
          close(in1);
          continue;
        }

        if (FLAG(u))
          xconnect(in1, (void *)toybuf, sizeof(struct sockaddr_storage));

        // Cleanup and redirect for exec
        if (toys.optc) {
          // Do we need a tty?
// TODO nommu and -t only affects server mode...
//        if (FLAG(t)) child = forkpty(&fdout, NULL, NULL, NULL);

          close(sockfd);
          dup2(in1, 0);
          dup2(in1, 1);
          if (FLAG(E)) dup2(in1, 2);
          if (in1>2) close(in1);
          xexec(toys.optargs);

        // Copy stdin/out
        } else {
          pollinate(in1, in2, out1, out2, TT.W, TT.q);
          close(in1);
        }
      } while (FLAG(L));
    }
  }

cleanup:
  if (CFG_TOYBOX_FREE) {
    close(in1);
    close(sockfd);
  }
}
