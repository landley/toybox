/* netcat.c - Forward stdin/stdout to a file or network connection.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * TODO: udp, ipv6, genericize for telnet/microcom/tail-f
 * fix -t, xconnect
 * netcat -L zombies

USE_NETCAT(OLDTOY(nc, netcat, TOYFLAG_USR|TOYFLAG_BIN))
USE_NETCAT(NEWTOY(netcat, USE_NETCAT_LISTEN("^tlL")"w#<1W#<1p#<1>65535q#<1s:f:46uU"USE_NETCAT_LISTEN("[!tlL][!Lw]")"[!46U]", TOYFLAG_BIN))

config NETCAT
  bool "netcat"
  default y
  help
    usage: netcat [-46U] [-u] [-wpq #] [-s addr] {IPADDR PORTNUM|-f FILENAME}

    Forward stdin/stdout to a file or network connection.

    -4	Force IPv4
    -6	Force IPv6
    -f	Use FILENAME (ala /dev/ttyS0) instead of network
    -p	Local port number
    -q	Quit SECONDS after EOF on stdin, even if stdout hasn't closed yet
    -s	Local source address
    -u	Use UDP
    -U	Use a UNIX domain socket
    -w	SECONDS timeout to establish connection
    -W	SECONDS timeout for more data on an idle connection

    Use "stty 115200 -F /dev/ttyS0 && stty raw -echo -ctlecho" with
    netcat -f to connect to a serial port.

config NETCAT_LISTEN
  bool "netcat server options (-let)"
  default y
  depends on NETCAT
  help
    usage: netcat [-t] [-lL COMMAND...]

    -l	Listen for one incoming connection
    -L	Listen for multiple incoming connections (server mode)
    -t	Allocate tty (must come before -l or -L)

    The command line after -l or -L is executed (as a child process) to handle
    each incoming connection. If blank -l waits for a connection and forwards
    it to stdin/stdout. If no -p specified, -l prints port it bound to and
    backgrounds itself (returning immediately).

    For a quick-and-dirty server, try something like:
    netcat -s 127.0.0.1 -p 1234 -tL /bin/bash -l
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

static void set_alarm(int seconds)
{
  xsignal(SIGALRM, seconds ? timeout : SIG_DFL);
  alarm(seconds);
}

void netcat_main(void)
{
  int sockfd = -1, in1 = 0, in2 = 0, out1 = 1, out2 = 1;
  int family = AF_UNSPEC, type = SOCK_STREAM;
  pid_t child;

  // Addjust idle and quit_delay to ms or -1 for no timeout
  TT.W = TT.W ? TT.W*1000 : -1;
  TT.q = TT.q ? TT.q*1000 : -1;

  set_alarm(TT.w);

  // The argument parsing logic can't make "<2" conditional on other
  // arguments like -f and -l, so do it by hand here.
  if ((toys.optflags&FLAG_f) ? toys.optc :
      (!(toys.optflags&(FLAG_l|FLAG_L)) &&
       toys.optc!=(toys.optflags&FLAG_U?1:2)))
        help_exit("bad argument count");

  if (toys.optflags&FLAG_4) family = AF_INET;
  else if (toys.optflags&FLAG_6) family = AF_INET6;
  else if (toys.optflags&FLAG_U) family = AF_UNIX;

  if (toys.optflags&FLAG_u) type = SOCK_DGRAM;

  if (TT.f) in1 = out2 = xopen(TT.f, O_RDWR);
  else {
    // Setup socket
    if (!(toys.optflags&(FLAG_L|FLAG_l))) {
      if (toys.optflags&FLAG_U) {
        struct sockaddr_un sockaddr;

        memset(&sockaddr, 0, sizeof(struct sockaddr_un));

        if (strlen(toys.optargs[0]) + 1 > sizeof(sockaddr.sun_path))
          error_exit("socket path too long %s", toys.optargs[0]);
        strcpy(sockaddr.sun_path, toys.optargs[0]);
        sockaddr.sun_family = AF_UNIX;

        sockfd = xsocket(AF_UNIX, type | SOCK_CLOEXEC, 0);
        xconnect(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
      } else {
        sockfd = xconnectany(xgetaddrinfo(toys.optargs[0], toys.optargs[1],
                                          family, type, 0, 0));
      }

      // We have a connection. Disarm timeout.
      set_alarm(0);

      in1 = out2 = sockfd;

      pollinate(in1, in2, out1, out2, TT.W, TT.q);
    } else {
      // Listen for incoming connections
      if (toys.optflags&FLAG_U) {
        struct sockaddr_un sockaddr;

        memset(&sockaddr, 0, sizeof(struct sockaddr_un));

        if (!(toys.optflags&FLAG_s))
          error_exit("-s must be provided if using -U with -L/-l");

        if (strlen(TT.s) + 1 > sizeof(sockaddr.sun_path))
          error_exit("socket path too long %s", TT.s);
        strcpy(sockaddr.sun_path, TT.s);
        sockaddr.sun_family = AF_UNIX;

        sockfd = xsocket(AF_UNIX, type | SOCK_CLOEXEC, 0);
        xbind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
      } else {
        sprintf(toybuf, "%ld", TT.p);
        sockfd = xbindany(xgetaddrinfo(TT.s, toybuf, family, type, 0, 0));
      }

      if (listen(sockfd, 5)) error_exit("listen");
      if (!TT.p && !(toys.optflags&FLAG_U)) {
        struct sockaddr* address = (void*)toybuf;
        socklen_t len = sizeof(struct sockaddr_storage);
        short port_be;

        getsockname(sockfd, address, &len);
        if (address->sa_family == AF_INET)
          port_be = ((struct sockaddr_in*)address)->sin_port;
        else if (address->sa_family == AF_INET6)
          port_be = ((struct sockaddr_in6*)address)->sin6_port;
        else
          perror_exit("getsockname: bad family");

        printf("%d\n", SWAP_BE16(port_be));
        fflush(stdout);
        // Return immediately if no -p and -Ll has arguments, so wrapper
        // script can use port number.
        if (CFG_TOYBOX_FORK && toys.optc && xfork()) goto cleanup;
      }

      do {
        child = 0;
        in1 = out2 = accept(sockfd, NULL, NULL);
        if (in1<0) perror_exit("accept");

        // We have a connection. Disarm timeout.
        set_alarm(0);

        if (toys.optc) {
          // Do we need a tty?

// TODO nommu, and -t only affects server mode...? Only do -t with optc
//        if (CFG_TOYBOX_FORK && (toys.optflags&FLAG_t))
//          child = forkpty(&fdout, NULL, NULL, NULL);
//        else

          // Do we need to fork and/or redirect for exec?

          if (toys.optflags&FLAG_L) NOEXIT(child = XVFORK());
          if (child) {
            close(in1);
            continue;
          }
          dup2(in1, 0);
          dup2(in1, 1);
          if (toys.optflags&FLAG_L) dup2(in1, 2);
          if (in1>2) close(in1);
          xexec(toys.optargs);
        }

        pollinate(in1, in2, out1, out2, TT.W, TT.q);
        close(in1);
      } while (!(toys.optflags&FLAG_l));
    }
  }

cleanup:
  if (CFG_TOYBOX_FREE) {
    close(in1);
    close(sockfd);
  }
}
