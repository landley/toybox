/* vi: set sw=4 ts=4:
 *
 * nc: mini-netcat - Forward stdin/stdout to a file or network connection.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_NETCAT(OLDTOY(nc, netcat, USE_NETCAT_LISTEN("tl^L^")"w#p#s:q#f:", TOYFLAG_BIN))
USE_NETCAT(NEWTOY(netcat, USE_NETCAT_LISTEN("tl^L^")"w#p#s:q#f:", TOYFLAG_BIN))

config NETCAT
	bool "netcat"
	default y
	help
	  usage: netcat [-wpq #] [-s addr] {IPADDR PORTNUM|-f FILENAME|-let} [-e COMMAND]

	  -w	SECONDS timeout for connection
	  -p	local port number
	  -s	local ipv4 address
	  -q	SECONDS quit this many seconds after EOF on stdin.
	  -f	use FILENAME (ala /dev/ttyS0) instead of network

	  Use "stty 115200 -F /dev/ttyS0 && stty raw -echo -ctlecho" with
	  netcat -f to connect to a serial port.


config NETCAT_LISTEN
	bool "netcat sever options (-let)"
	default y
	depends on NETCAT
	help
	  -t    allocate tty (must come before -l or -L)
	  -l	listen for one incoming connection.
	  -L	listen for multiple incoming connections (server mode).

	  Any additional command line arguments after -l or -L are executed
	  to handle each incoming connection.  If none, the connection is
	  forwarded to stdin/stdout.

	  For a quick-and-dirty server, try something like:
		netcat -s 127.0.0.1 -p 1234 -tL /bin/bash -l
*/

#include "toys.h"
#include "toynet.h"

DEFINE_GLOBALS(
	char *filename;        // -f read from filename instead of network
	long quit_delay;       // -q Exit after EOF from stdin after # seconds.
	char *source_address;  // -s Bind to a specific source address.
	long port;             // -p Bind to a specific source port.
	long wait;             // -w Wait # seconds for a connection.
)

#define TT this.netcat

#define FLAG_f   1
#define FLAG_L  32
#define FLAG_l  64
#define FLAG_t 128

static void timeout(int signum)
{
	if (TT.wait) error_exit("Timeout");
	exit(0);
}

static void set_alarm(int seconds)
{
	signal(SIGALRM, seconds ? timeout : SIG_DFL);
	alarm(seconds);
}

// Translate x.x.x.x numeric IPv4 address, or else DNS lookup an IPv4 name.
static void lookup_name(char *name, uint32_t *result)
{
	struct hostent *hostbyname;

	hostbyname = gethostbyname(name);
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
	int sockfd=-1, pollcount=2;
	struct pollfd pollfds[2];

	memset(pollfds, 0, 2*sizeof(struct pollfd));
	pollfds[0].events = pollfds[1].events = POLLIN;
	set_alarm(TT.wait);

	// The argument parsing logic can't make "<2" conditional on other
	// arguments like -f and -l, so we do it by hand here.
	if (toys.optflags&FLAG_f) {
		if (toys.optc) toys.exithelp++;
	} else if (!(toys.optflags&(FLAG_l|FLAG_L)) && toys.optc!=2) toys.exithelp++;

	if (toys.exithelp) error_exit("Argument count wrong");

	if (TT.filename) pollfds[0].fd = xopen(TT.filename, O_RDWR);
	else {
		int temp;
		struct sockaddr_in address;

		// Setup socket
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (-1 == sockfd) perror_exit("socket");
		fcntl(sockfd, F_SETFD, FD_CLOEXEC);
		temp = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &temp, sizeof(temp));
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		if (TT.source_address || TT.port) {
			address.sin_port = SWAP_BE16(TT.port);
			if (TT.source_address) 
				lookup_name(TT.source_address, (uint32_t *)&address.sin_addr);
			if (bind(sockfd, &address, sizeof(address)))
				perror_exit("bind");
		}

		// Dial out

		if (!CFG_NETCAT_LISTEN || !(toys.optflags&(FLAG_L|FLAG_l))) {
			// Figure out where to dial out to.
			lookup_name(*toys.optargs, (uint32_t *)&address.sin_addr);
			lookup_port(toys.optargs[1], &address.sin_port);
			temp = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
			if (temp<0) perror_exit("connect");
			pollfds[0].fd = sockfd;

		// Listen for incoming connections

		} else {
			socklen_t len = sizeof(address);

			if (listen(sockfd, 5)) error_exit("listen");
			if (!TT.port) {
				getsockname(sockfd, &address, &len);
				printf("%d\n", SWAP_BE16(address.sin_port));
				fflush(stdout);
			}
			// Do we need to return immediately because -l has arguments?

			if ((toys.optflags&FLAG_l) && toys.optc) {
				if (fork()) goto cleanup;
				close(0);
				close(1);
				close(2);
			}

			for (;;) {
				pid_t child = 0;

				// For -l, call accept from the _new_ thread.

				pollfds[0].fd = accept(sockfd, (struct sockaddr *)&address,
					&len);
				if (pollfds[0].fd<0) perror_exit("accept");

				// Do we need a tty?

				if (toys.optflags&FLAG_t)
					child = forkpty(&(pollfds[1].fd), NULL, NULL, NULL);

				// Do we need to fork and/or redirect for exec?

				else {
					if (toys.optflags&FLAG_L) child = fork();
					if (!child && toys.optc) {
						int fd = pollfds[0].fd;

						if (!temp) close(sockfd);
						dup2(fd, 0);
						dup2(fd, 1);
						dup2(fd, 2);
						if (fd>2) close(fd);
					}
				}

				if (child<0) error_msg("Fork failed\n");
				if (child<1) break;
				close(pollfds[0].fd);
			}
		}
	}

	// We have a connection.  Disarm timeout.
	// (Does not play well with -L, but what _should_ that do?)
	set_alarm(0);

	if (CFG_NETCAT_LISTEN && (toys.optflags&(FLAG_L|FLAG_l) && toys.optc)) {
		execvp(*toys.optargs, toys.optargs);
		error_exit("Exec failed");
	}

	// Poll loop copying stdin->socket and socket->stdout.
	for (;;) {
		int i;

		if (0>poll(pollfds, pollcount, -1)) perror_exit("poll");

		for (i=0; i<pollcount; i++) {
			if (pollfds[i].revents & POLLIN) {
				int len = read(pollfds[i].fd, toybuf, sizeof(toybuf));
				if (len<1) goto dohupnow;
				xwrite(i ? pollfds[0].fd : 1, toybuf, len);
			} else if (pollfds[i].revents & POLLHUP) {
dohupnow:
				// Close half-connection.  This is needed for things like
				// "echo GET / | netcat landley.net 80"
				if (i) {
					shutdown(pollfds[0].fd, SHUT_WR);
					pollcount--;
					set_alarm(TT.quit_delay);
				} else goto cleanup;
			}
		}
	}
cleanup:
	if (CFG_TOYBOX_FREE) {
		close(pollfds[0].fd);
		close(sockfd);
	}
}
