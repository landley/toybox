/* sntp.c - sntp client and server
 *
 * Copyright 2019 Rob Landley <rob@landley.net>
 *
 * See https://www.ietf.org/rfc/rfc4330.txt

  modes: oneshot display, oneshot set, persist, serve, multi

USE_SNTP(NEWTOY(sntp, "m:Sp:asdDqr#<4>17=10[!as]", TOYFLAG_USR|TOYFLAG_BIN))

config SNTP
  bool "sntp"
  default n
  help
    usage: sntp [-saSdDqm] [-r SHIFT] [-m ADDRESS] [-p PORT] [SERVER]

    Simple Network Time Protocol client. Query SERVER and display time.

    -p	Use PORT (default 123)
    -s	Set system clock suddenly
    -a	Adjust system clock gradually
    -S	Serve time instead of querying (bind to SERVER address if specified)
    -m	Wait for updates from multicast ADDRESS (RFC 4330 says use 224.0.1.1)
    -d	Daemonize (run in background re-querying )
    -D	Daemonize but stay in foreground: re-query time every 1000 seconds
    -r	Retry shift (every 1<<SHIFT seconds)
    -q	Quiet (don't display time)
*/

#define FOR_sntp
#include "toys.h"

GLOBALS(
  long r;
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

// Get time and return ntptime (saving timespec in pointer if not null)
// NTP time is high 32 bits = seconds since 1970 (blame RFC 868), low 32 bits
// fraction of a second.
// diff is how far off we think our clock is from reality (in nanoseconds)
static unsigned long long lunchtime(struct timespec *television, long long diff)
{
  struct timespec tv;

  clock_gettime(CLOCK_REALTIME, &tv);
  if (diff) nanomove(&tv, diff);

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

void sntp_main(void)
{
  struct timespec tv, tv2;
  unsigned long long *pktime = (void *)toybuf, now, then, before;
  long long diff = 0;
  struct addrinfo *ai;
  union socksaddr sa;
  int fd, tries = 0;

  if (!(FLAG(S)||FLAG(m)) && !*toys.optargs)
    error_exit("Need -Sm or SERVER address");

  // Lookup address and open server or client UDP socket
  if (!TT.p || !*TT.p) TT.p = "123";
  ai = xgetaddrinfo(*toys.optargs, TT.p, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP,
    AI_PASSIVE*!*toys.optargs);

  if (FLAG(d) && daemon(0, 0)) perror_exit("daemonize");

  // Act as server if necessary
  if (FLAG(S)|FLAG(m)) {
    fd = xbind(ai);
    if (TT.m) {
      struct ip_mreq group;

      // subscribe to multicast group
      memset(&group, 0, sizeof(group));
      group.imr_multiaddr.s_addr = inet_addr(TT.m);
      xsetsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));
    }
  } else fd = xsocket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);

  // -Sm = loop waiting for input
  // -Dd = loop polling time and waiting until next poll period
  // Otherwise poll up to 3 times to get 2 responses, then exit

  // loop sending/receiving packets
  for (;;) {
    now = millitime();

    // Figure out if we're in server and multicast modes don't poll
    if (FLAG(m) || FLAG(S)) then = -1;

    // daemon and oneshot modes send a packet each time through outer loop
    else {
      then = now + 3000;
      if (FLAG(d) || FLAG(D)) then = now + (1<<TT.r)*1000;

      // Send NTP query packet
      memset(toybuf, 0, 48);
      *toybuf = 0xe3; // li = 3 (unsynchronized), version = 4, mode = 3 (client)
      toybuf[2] = 8;  // poll frequency 1<<8 = 256 seconds
      pktime[5] = SWAP_BE64(before = lunchtime(&tv, diff));
      xsendto(fd, toybuf, 48, ai->ai_addr);
    }

    // Loop receiving packets until it's time to send the next one.
    for (;;) {
      int strike;

      // Wait to receive a packet

      if (then>0 && then<(now = millitime())) break;;
      strike = xrecvwait(fd, toybuf, sizeof(toybuf), &sa, then-now);
      if (strike<1) {
        if (!(FLAG(S)||FLAG(m)||FLAG(D)||FLAG(d)) && ++tries == 3)
          error_exit("no reply from %s", *toys.optargs);
        break;
      }
      if (strike<48) continue;

      // Validate packet
      if (!FLAG(S) || FLAG(m)) {
        char buf[128];
        int mode = 7&*toybuf;

        // Is source address what we expect?
        xstrncpy(buf, ntop(ai->ai_addr), 128);
        strike = strcmp(buf, ntop((void *)&sa));
        // Does this reply's orignate timestamp match the packet we sent?
        if (!FLAG(S) && !FLAG(m) && before != SWAP_BE64(pktime[3])) continue;
        // Ignore packets from wrong address or with wrong mode
        if (strike && !FLAG(S)) continue;
        if (!(FLAG(m) && mode==5 || FLAG(S) && mode==3 ||
            !FLAG(m) && !FLAG(S) && mode==4)) continue;
      }

      // If received a -S request packet, send server packet
      if (strike) {
        char *buf = toybuf+48;

        *buf = 0x24;  // LI 0 VN 4 mode 4.
        buf[1] = 3;   // stratum 3
        buf[2] = 10;  // recommended retry every 1<<10=1024 seconds
        buf[3] = 250; // precision -6, minimum allowed
        strcpy(buf+12, "LOCL");
        pktime[6+3] = pktime[5]; // send back reference time they sent us
        // everything else is current time
        pktime[6+2] = pktime[6+4] = pktime[6+5] = SWAP_BE64(lunchtime(0, 0));
        xsendto(fd, buf, 48, (void *)&sa);

      // Got a time packet from a recognized server
      } else {
        int unset = !diff;

        // First packet: figure out how far off our clock is from what server
        // said and try again. Don't set clock, just record offset to use
        // generating second reuest. (We know this time is in the past
        // because transmission took time, but it's a start. And if time is
        // miraculously exact, don't loop.)
 
        lunchtime(&tv2, diff);
        diff = nanodiff(&tv, &tv2);
        if (unset && diff) break;

        // Second packet: determine midpoint of packet transit time according
        // to local clock, assuming each direction took same time so midpoint
        // is time server reported. The first television was the adjusted time
        // we sent the packet at, tv2 is what server replied, so now diff
        // is round trip time.

        // What time did the server say and how far off are we?
        nanomove(&tv, diff/2);
        doublyso(SWAP_BE64(pktime[5]), &tv2);
        diff = nanodiff(&tv, &tv2);

        if (FLAG(s)) {
          // Do read/adjust/set to lose as little time as possible.
          clock_gettime(CLOCK_REALTIME, &tv2);
          nanomove(&tv2, diff);
          if (clock_settime(CLOCK_REALTIME, &tv2))
            perror_exit("clock_settime");
        } else if (FLAG(a)) {
          struct timeval why;

          // call adjtime() to move the clock gradually, copying nanoseconds
          // into gratuitous microseconds structure for sad historical reasons
          memset(&tv2, 0, sizeof(tv2));
          nanomove(&tv2, diff);
          why.tv_sec = tv2.tv_sec;
          why.tv_usec = tv2.tv_nsec/1000;
          if (adjtime(&why, 0)) perror_exit("adjtime");
        }

        // Display the time and offset
        if (!FLAG(q)) {
          format_iso_time(toybuf, sizeof(toybuf)-1, &tv2);
          printf("%s offset %c%d.%09d secs\n", toybuf, (diff<0) ? '-' : '+',
            abs(diff/1000000000), abs(diff%1000000000));
        }

        // If we're not in daemon mode, we're done. (Can't get here for -S.)
        if (!FLAG(d) && !FLAG(D)) return;
      }
    }
  }
}
