/* netcat.c - Forward stdin/stdout to a file or network connection.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * TODO: udp, ipv6, genericize for telnet/microcom/tail-f

USE_NETCAT(OLDTOY(nc, netcat, TOYFLAG_USR|TOYFLAG_BIN))
USE_NETCAT(NEWTOY(netcat, USE_NETCAT_LISTEN("^tlL")"w#W#p#s:q#f:"USE_NETCAT_LISTEN("[!tlL][!Lw]"), TOYFLAG_BIN))

config NETCAT
  bool "netcat"
  default y
  help
    usage: netcat [-u] [-wpq #] [-s addr] {IPADDR PORTNUM|-f FILENAME}

    -f	use FILENAME (ala /dev/ttyS0) instead of network
    -p	local port number
    -q	quit SECONDS after EOF on stdin, even if stdout hasn't closed yet
    -s	local source address
    -w	SECONDS timeout to establish connection
    -W	SECONDS timeout for idle connection

    Use "stty 115200 -F /dev/ttyS0 && stty raw -echo -ctlecho" with
    netcat -f to connect to a serial port.

config NETCAT_LISTEN
  bool "netcat server options (-let)"
  default y
  depends on NETCAT
  help
    usage: netcat [-t] [-lL COMMAND...]

    -l	listen for one incoming connection
    -L	listen for multiple incoming connections (server mode)
    -t	allocate tty (must come before -l or -L)

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
  char *filename;        // -f read from filename instead of network
  long quit_delay;       // -q Exit after EOF from stdin after # seconds.
  char *source_address;  // -s Bind to a specific source address.
  long port;             // -p Bind to a specific source port.
  long idle;             // -W Wait # seconds for more data
  long wait;             // -w Wait # seconds for a connection.
)

static void timeout(int signum)
{
  if (TT.wait) error_exit("Timeout");
  // This should be xexit() but would need siglongjmp()...
  exit(0);
}

static void set_alarm(int seconds)
{
  xsignal(SIGALRM, seconds ? timeout : SIG_DFL);
  alarm(seconds);
}

// Translate x.x.x.x numeric IPv4 address, or else DNS lookup an IPv4 name.
static void lookup_name(char *name, uint32_t *result)
{
  struct hostent *hostbyname;

  hostbyname = gethostbyname(name); // getaddrinfo
  if (!hostbyname) error_exit("no host '%s'", name);
  *result = *(uint32_t *)*hostbyname->h_addr_list;
}

// Worry about a fancy lookup later.
static void lookup_port(char *str, uint16_t *port)
{
  *port = SWAP_BE16(atoi(str));
}

void netcat_main(void)
{
  struct sockaddr_in *address = (void *)toybuf;
  int sockfd=-1, in1 = 0, in2 = 0, out1 = 1, out2 = 1;
  pid_t child;

  // Addjust idle and quit_delay to miliseconds or -1 for no timeout
  TT.idle = TT.idle ? TT.idle*1000 : -1;
  TT.quit_delay = TT.quit_delay ? TT.quit_delay*1000 : -1;

  set_alarm(TT.wait);

  // The argument parsing logic can't make "<2" conditional on other
  // arguments like -f and -l, so we do it by hand here.
  if ((toys.optflags&FLAG_f) ? toys.optc :
      (!(toys.optflags&(FLAG_l|FLAG_L)) && toys.optc!=2))
        help_exit("bad argument count");

  if (TT.filename) in1 = out2 = xopen(TT.filename, O_RDWR);
  else {
    // Setup socket
    sockfd = xsocket(AF_INET, SOCK_STREAM, 0);
    fcntl(sockfd, F_SETFD, FD_CLOEXEC);
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &out1, sizeof(out1));
    address->sin_family = AF_INET;
    if (TT.source_address || TT.port) {
      address->sin_port = SWAP_BE16(TT.port);
      if (TT.source_address)
        lookup_name(TT.source_address, (uint32_t *)&(address->sin_addr));
      if (bind(sockfd, (struct sockaddr *)address, sizeof(*address)))
        perror_exit("bind");
    }

    // Dial out

    if (!CFG_NETCAT_LISTEN || !(toys.optflags&(FLAG_L|FLAG_l))) {
      // Figure out where to dial out to.
      lookup_name(*toys.optargs, (uint32_t *)&(address->sin_addr));
      lookup_port(toys.optargs[1], &(address->sin_port));
// TODO xconnect
      if (connect(sockfd, (struct sockaddr *)address, sizeof(*address))<0)
        perror_exit("connect");
      in1 = out2 = sockfd;

    // Listen for incoming connections

    } else {
      socklen_t len = sizeof(*address);

      if (listen(sockfd, 5)) error_exit("listen");
      if (!TT.port) {
        getsockname(sockfd, (struct sockaddr *)address, &len);
        printf("%d\n", SWAP_BE16(address->sin_port));
        fflush(stdout);
        // Return immediately if no -p and -Ll has arguments, so wrapper
        // script can use port number.
        if (CFG_TOYBOX_FORK && toys.optc && xfork()) goto cleanup;
      }

      for (;;) {
        child = 0;
        len = sizeof(*address); // gcc's insane optimizer can overwrite this
        in1 = out2 = accept(sockfd, (struct sockaddr *)address, &len);

        if (in1<0) perror_exit("accept");

        // We can't exit this loop or the optimizer's "liveness analysis"
        // combines badly with vfork() to corrupt or local variables
        // (the child's call stack gets trimmed and the next function call
        // stops the variables the parent tries to re-use next loop)
        // So there's a bit of redundancy here

        // We have a connection. Disarm timeout.
        set_alarm(0);

        if (toys.optc) {
          // Do we need a tty?

// TODO nommu, and -t only affects server mode...? Only do -t with optc
//        if (CFG_TOYBOX_FORK && (toys.optflags&FLAG_t))
//          child = forkpty(&fdout, NULL, NULL, NULL);
//        else

          // Do we need to fork and/or redirect for exec?

          if (toys.optflags&FLAG_L) {
            toys.stacktop = 0;
            child = vfork();
          }
          if (child<0) error_msg("vfork failed\n");
          else {
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
        }

        pollinate(in1, in2, out1, out2, TT.idle, TT.quit_delay);
        close(in1);
      }
    }
  }

  // We have a connection. Disarm timeout.
  set_alarm(0);

  pollinate(in1, in2, out1, out2, TT.idle, TT.quit_delay);

cleanup:
  if (CFG_TOYBOX_FREE) {
    close(in1);
    close(sockfd);
  }
}
