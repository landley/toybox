/* vi: set sw=4 ts=4:
 *
 * nc: mini-netcat - Forward stdin/stdout to a file or network connection.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

config NETCAT
	bool "netcat"
	default n
	help
	  usage: netcat [-iwlp] {IPADDR PORTNUM|-f FILENAME} [-e COMMAND]

	  -e	exec the rest of the command line
	  -i	SECONDS delay after each line sent
	  -w	SECONDS timeout for connection
	  -f	filename use file (ala /dev/ttyS0) instead of network
	  -l	listen for incoming connection (twice for persistent connection)
	  -p	local port number
	  -s	local source address
	  -q	SECONDS quit this many seconds after EOF on stdin.

	  Use -l twice with -e for a quick-and-dirty server.

	  Use "stty 115200 -F /dev/ttyS0 && stty raw -echo -ctlecho" with
	  netcat -f to connect to a serial port.
*/

#include "toys.h"
#include "toynet.h"

#define TT toy.netcat

static void timeout(int signum)
{
	error_exit("Timeout");
}

// Translate x.x.x.x numeric IPv4 address, or else DNS lookup an IPv4 name.
void lookup_name(char *name, uint32_t *result)
{
	struct hostent *hostbyname;

	hostbyname = gethostbyname(*toys.optargs);
	if (!hostbyname) error_exit("name lookup failed");
	*result = *(uint32_t *)*hostbyname->h_addr_list;
}

// Worry about a fancy lookup later.
void lookup_port(char *str, uint16_t *port)
{
  *port = SWAP_BE16(atoi(str));
}

void netcat_main(void)
{
	int sockfd, pollcount;
	struct pollfd pollfds[2];

	if (TT.wait) {
		signal(SIGALRM, timeout);
		alarm(TT.wait);
	}

	if (TT.filename) pollfds[0].fd = xopen(TT.filename, O_RDWR);
	else {
		int temp;
		struct sockaddr_in address;

		// The argument parsing logic can't make "<2" conditional on "-f", so...
		if (!*toys.optargs || !toys.optargs[1]) {
			toys.exithelp++;
			error_exit("Need address and port");
		}

		// Setup socket
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (-1 == sockfd) perror_exit("socket");
		fcntl(sockfd, F_SETFD, FD_CLOEXEC);
		temp = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &temp, sizeof(temp));
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		if (TT.port) {
			address.sin_port = TT.port;
			if (-1 == bind(sockfd, &address, sizeof(address)))
				perror_exit("bind");
		}

		// Figure out where to dial out to.
		lookup_name(*toys.optargs, (uint32_t *)&address.sin_addr);
		lookup_port(toys.optargs[1], &address.sin_port);
		temp = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
		if (temp<0) perror_exit("connect");
		pollfds[0].fd = sockfd;
	}

	// We have a connection.  Disarm timeout.
	if (TT.wait) {
		alarm(0);
		signal(SIGALRM, SIG_DFL);
	}

	pollcount = 2;
	pollfds[1].fd = 0;
	pollfds[0].events = pollfds[1].events = POLLIN;

	// Poll loop copying stdin->socket and socket->stdout.
	for (;;) {
		int i;

		if (0>poll(pollfds, pollcount, -1)) perror_exit("poll");

		for (i=0; i<pollcount; i++) {
			if (pollfds[i].revents & POLLIN) {
				int len = read(pollfds[i].fd, toybuf, sizeof(toybuf));
				if (len<1) goto dohupnow;
				xwrite(i ? pollfds[0].fd : 1, toybuf, len);
			}
			if (pollfds[i].revents & POLLHUP) {
dohupnow:
				// Close half-connect.  This is needed for things like
				// "echo GET / | netcat landley.net 80" to work.
				if (i) {
					shutdown(pollfds[0].fd, SHUT_WR);
					pollcount--;
				} else goto cleanup;
			}
		}
	}
cleanup:
	close(pollfds[0].fd);
//	close(sockfd);
}
