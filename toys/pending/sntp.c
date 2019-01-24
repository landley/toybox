/* sntp.c - sntp client and server
 *
 * Copyright 2019 Rob Landley <rob@landley.net>
 *
 * See https://www.ietf.org/rfc/rfc4330.txt

  modes: oneshot display, oneshot set, persist, serve, multi

  verify source addr
  serve time
    - precision -6
    - source LOCL
    - copy source packet transmit to originate (5->3), 2=4=5
  wait for multicast updates
  uniclient: retry at poll interval

USE_SNTP(NEWTOY(sntp, "m:Sp:asdD[!as]", TOYFLAG_USR|TOYFLAG_BIN))

config SNTP
  bool "sntp"
  default n
  help
    usage: sntp [-saS] [-dD[-m ADDRESS] [-p PORT] [SERVER]

    Simple Network Time Protocol client. Query SERVER and display time.

    -p	Use PORT (default 123)
    -s	Set system clock suddenly
    -a	Adjust system clock gradually
    -S	Serve time instead of querying (bind to SERVER address if specified)
    -m	Wait for updates from multicast ADDRESS (RFC 4330 says use 224.0.1.1)
    -d	Daemonize (run in background re-querying )
    -D	Daemonize but stay in foreground: re-query time every 1000 seconds
*/

#define FOR_sntp
#include "toys.h"

GLOBALS(
  char *p, *m;
)

// Seconds from 1900 to 1970, including appropriate leap days
#define SEVENTIES 2208988800L

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

// Get time and return ntptime (saving timespec in pointer if not null)
// NTP time is high 32 bits = seconds since 1970 (blame RFC 868), low 32 bits
// fraction of a second.
static unsigned long long lunchtime(struct timespec *television)
{
  struct timespec tv;

  clock_gettime(CLOCK_REALTIME, &tv);

  if (television) *television = tv;

  // Unix time is 1970 but RFCs 868 and 958 said 1900 so add seconds 1900->1970
  // If they'd done a 34/30 bit split the Y2036 problem would be centuries
  // from now and still give us nanosecond accuracy, but no...
  return ((tv.tv_sec+SEVENTIES)<<32)+(((long long)tv.tv_nsec)<<32)/1000000000;
}

// convert ntptime back to struct timespec.
static void doublyso(unsigned long long now, struct timespec *tv)
{
  // Y2036 fixup: if time wrapped, it's in the future
  tv->tv_sec = (now>>32) + (1LL<<32)*!(now&(1LL<<63));
  tv->tv_sec -= SEVENTIES; // Force signed math for Y2038 fixup
  tv->tv_nsec = ((now&0xFFFFFFFF)*1000000000)>>32;
}

// return difference between two timespecs in nanosecs
static long long nanodiff(struct timespec *old, struct timespec *new)
{
  return (new->tv_sec - old->tv_sec)*1000000000LL+(new->tv_nsec - old->tv_nsec);
}

// Adjust timespec by nanosecond offset
static void nanomove(struct timespec *ts, long long offset)
{
  long long nano = ts->tv_nsec + offset, secs = nano/1000000000;

  ts->tv_sec += secs;
  nano %= 1000000000;
  if (nano<0) {
    ts->tv_sec--;
    nano += 1000000000;
  }
  ts->tv_nsec = nano;
}

static void server_packet(char *buf, unsigned long long ref)
{
  *buf = 0x24;
  buf[1] = 3;
  buf[2] = 10;
  buf[3] = 253;
  strcpy(buf+12, "LOCL");
  pktime[3] = ref;
  pktime[2] = pktime[4] = pktime[5] = newtime;
}

void sntp_main(void)
{
  struct timespec tv, tv2;
  unsigned long long *pktime = (void *)toybuf, now, then, before;
  long long diff;
  long diffsecs, diffnano;
  struct addrinfo *ai;
  union socksaddr sa;
  int fd, tries = 0;

  if (!(FLAG(S)||FLAG(m)) && !*toys.optargs)
    error_exit("Need -Sm or SERVER address");

  // Lookup address and open server or client UDP socket
  if (!TT.p || !*TT.p) TT.p = "123";
  ai = xgetaddrinfo(*toys.optargs, TT.p, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP,
    AI_PASSIVE*!*toys.optargs);

  if (FLAG(d)) daemon(0,0);

  // Act as server if necessary
  if (FLAG(S)|FLAG(m)) {
    fd = xbind(ai);
    if (TT.m) {
      struct ip_mreq group;

      // subscribe to multicast group
      memset(&group, 0, sizeof(group));
      group.imr_multiaddr.s_addr = inet_addr(addr);
      xsetsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&group,
        sizeof(group));
    }
  } else fd = xsocket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);

// Sm - loop waiting for input
// Dd - loop polling with probe
// else poll 3 times
// if SmDd loop

  for (;;) {

    // Daemon mode isn't limited to 3 tries
    if (FLAG(d) || FLAG(D)) {
      then = retry_timeout;
    } else if (++tries>3) break;

    // Prepare outgoing NTP packet
    memset(toybuf, 0, 48);
    *toybuf = 0xe3; // li = 3 (unsynchronized), version = 4, mode = 3 (client)
    toybuf[2] = 8;  // poll frequency 1<<8 = 256 seconds
    pktime[5] = SWAP_BE64(before = lunchtime(&tv));

    // Send and ye shall receive
    xsendto(fd, toybuf, 48, ai->ai_addr);
    then = (now = millitime())+4000;
    while (now<then) {
      // TODO: confirm sa matches
      if (48 == xrecvwait(fd, toybuf, sizeof(toybuf), &sa, then-now)) {
        if (TT.m || pktime[3] == SWAP_BE64(before)) break;
      }
      now = millitime();
    }
    if (now < then) break;
  }
  lunchtime(&tv2);

  // determine midpoint of packet transit time according to local clock
  // (simple calculation: assume each direction took same time so midpoint
  // is time reported by other clock)
  diff = nanodiff(&tv, &tv2)/2;
  nanomove(&tv, diff);

  doublyso(SWAP_BE64(pktime[5]), &tv2);
  diff = nanodiff(&tv, &tv2);
  diffsecs = diff/1000000000;
  diffnano = diff%1000000000;
  if (FLAG(s)) {
    clock_gettime(CLOCK_REALTIME, &tv);
    nanomove(&tv, diff);
    if (clock_settime(CLOCK_REALTIME, &tv)) perror_exit("clock_settime");
  } else if (FLAG(a)) {
    struct timeval tv = {diffsecs, diffnano/1000};

    if (adjtime(&tv, 0)) perror_exit("adjtime");
  } else {
    format_iso_time(toybuf, sizeof(toybuf)-1, &tv2);
    printf("%s offset %c%d.%09d secs\n", toybuf, (diff<0) ? '-' : '+',
      abs(diffsecs), abs(diffnano));
  }
}
