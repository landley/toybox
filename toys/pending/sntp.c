/* sntp.c - sntp client and server
 *
 * Copyright 2019 Rob Landley <rob@landley.net>
 *
 * See https://www.ietf.org/rfc/rfc4330.txt

USE_SNTP(NEWTOY(sntp, "sp:", TOYFLAG_USR|TOYFLAG_BIN))

config SNTP
  bool "sntp"
  default n
  help
    usage: sntp [-sm] [-p PORT] SERVER...

    Simple Network Time Protocol client, set system clock from a server.

    -p	Use PORT (default 123)
    -s	Serer
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

/// hardwired getaddrinfo() variant for what we want here.
static struct addrinfo *gai(char *server)
{
  return xgetaddrinfo(server, TT.p, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP, 0);
}

// Get time and return ntptime (saving timespec in pointer if not null)
// NTP time is high 32 bits = seconds since 1970 (blame RFC 868), low 32 bits
// fraction of a second.
static unsigned long long lunchtime(struct timespec *television)
{
  struct timespec tv;

  clock_gettime(CLOCK_REALTIME, &tv);

  if (television) *television = tv;

  // Unix time is 1970 but RFCs 868 and 958 said 1900, so add seconds 1900->1970
  // If they'd done a 34/30 bit split the Y2036 problem would be centuries
  // from now and still give us nanosecond accuracy, but no...
  return ((tv.tv_sec+SEVENTIES)<<32)+(((long long)tv.tv_nsec)<<32)/1000000000;
}

// convert ntptime back to struct timespec.
static void doublyso(unsigned long long now, struct timespec *tv)
{
  // Y2036 fixup: if time wrapped, it's in the future
  tv->tv_sec = (now>>32) + (1L<<32)*!(now&(1L<<63));
  tv->tv_sec -= SEVENTIES; // Force signed math for Y2038 fixup
  tv->tv_nsec = ((now&((1L<<32)-1))*1000000000)>>32;
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

int multicast = 0;

void sntp_main(void)
{
  struct timespec tv, tv2;
  unsigned long long *pktime = (void *)toybuf, now, then, before;
  long long diff;
  struct addrinfo *ai;
  union socksaddr sa;
  int fd, attempts;

  if (!FLAG(s) && !*toys.optargs) error_exit("Need -s or SERVER");

  // Lookup address and open server or client UDP socket
  if (!TT.p || !*TT.p) TT.p = "123";
  ai = gai(*toys.optargs);
  // When root, bind to local server address
  if (!getuid()) fd = xbind(gai(""));
  else fd = xsocket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);

  // Try 3 times
  for (attempts = 0; attempts < 3; attempts++) {
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
        if (multicast || pktime[3] == SWAP_BE64(before)) break;
      }
      now = millitime();
    }
    if (now < then) break;
  }
  lunchtime(&tv2);

//printf("before) %ld %ld\n", (long)tv.tv_sec, (long)tv.tv_nsec);
//printf("after) %ld %ld\n", (long)tv2.tv_sec, (long)tv2.tv_nsec);

  // determine midpoint of packet transit time according to local clock
  // (simple calculation: assume each direction took same time so midpoint
  // is time reported by other clock)
  diff = nanodiff(&tv, &tv2)/2;
//printf("halfoff = %lld %llx\n", diff, diff);
  nanomove(&tv, diff);
//printf("midpoint) %ld %ld\n", (long)tv.tv_sec, (long)tv.tv_nsec);

  doublyso(SWAP_BE64(pktime[5]), &tv2);
  diff = nanodiff(&tv2, &tv);
//printf("server) %ld %ld\n", (long)tv2.tv_sec, (long)tv2.tv_nsec);
//printf("offby = %lld\n", diff);

//int i;
//for (i=0; i<48; ) {
//  printf("%02x", toybuf[i]);
//  if (!(++i&15)) printf("\n");
//}

  format_iso_time(toybuf, sizeof(toybuf)-1, &tv);
  printf("%s offset %c%d.%09d secs\n", toybuf, (diff<0) ? '-' : '+',
    abs(diff/1000000000), abs(diff%1000000000));
}
