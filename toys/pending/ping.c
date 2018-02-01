/* ping.c - check network connectivity
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.
 *
 * Note: ping_group_range should never have existed. To disable it, do:
 *   echo 0 $(((1<<31)-1)) > /proc/sys/net/ipv4/ping_group_range
 * (Android does this by default in its init script.)
 *
 * Yes, I wimped out and capped -s at sizeof(toybuf), waiting for a complaint...
 
USE_PING(NEWTOY(ping, "<1>1t#<0>255=64c#<0=3s#<0>4088=56I:i:W#<0=10w#<0qf46[-46]", TOYFLAG_ROOTONLY|TOYFLAG_USR|TOYFLAG_BIN))
 
config PING
  bool "ping"
  default n
  help
    usage: ping [OPTIONS] HOST

    Check network connectivity by sending packets to a host and reporting
    its response.

    Send ICMP ECHO_REQUEST packets to ipv4 or ipv6 addresses and prints each
    echo it receives back, with round trip time. Returns true if host alive.

    Options:
    -4, -6      Force IPv4 or IPv6
    -c CNT      Send CNT many packets (default 3, 0 = infinite)
    -f          Flood (. on send, backspace on receive, to show packet drops)
    -i TIME     Interval between packets (default 1, need root for < .2)
    -I IFACE/IP Source interface or address
    -q          Quiet (stops after one returns true if host is alive)
    -s SIZE     Data SIZE in bytes (default 56)
    -t TTL      Set Time To Live (number of hops)
    -W SEC      Seconds to wait for response after -c (default 10)
    -w SEC      Exit after this many seconds
*/

#define FOR_ping 
#include "toys.h"

#include <ifaddrs.h>
#include <netinet/ip_icmp.h>

GLOBALS(
  long w;
  long W;
  char *i;
  char *I;
  long s;
  long c;
  long t;

  int sock;
  long i_ms;
)

static void xsendto(int sockfd, void *buf, size_t len, struct sockaddr *dest)
{
  int rc = sendto(TT.sock, buf, len, 0, dest,
    dest->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
      sizeof(struct sockaddr_in6));

  if (rc != len) perror_exit("sendto");
}

// assumes aligned and can read even number of bytes
static unsigned short pingchksum(unsigned short *data, int len)
{
  unsigned short u = 0, d;

  // circular carry is endian independent: bits from high byte go to low byte
  while (len>0) {
    d = *data++;
    if (len == 1) d &= 255<<IS_BIG_ENDIAN;
    if (d >= (u += d)) u++;
    len -= 2;
  }

  return u;
}

void ping_main(void)
{
  struct addrinfo *ai, *ai2;
  struct ifaddrs *ifa, *ifa2 = 0;
  union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } src_addr, src_addr2;
  struct sockaddr *sa = (void *)&src_addr, *sa2 = (void *)&src_addr2;
  struct pollfd pfd;
  int family = 0, sent = 0, len;
  long long tnext, tW, tnow, tw;
  unsigned short seq = 0;
  struct icmphdr *ih = (void *)toybuf;

  // Interval
  if (TT.i) {
    long frac;

    TT.i_ms = xparsetime(TT.i, 1000, &frac) * 1000;
    TT.i_ms += frac;
    if (TT.i_ms<200 && getuid()) error_exit("need root for -i <200");
  } else TT.i_ms = 1000;
  if (!(toys.optflags&FLAG_s)) TT.s = 56; // 64-PHDR_LEN

  // ipv4 or ipv6? (0 = autodetect if -I or arg have only one address type.)
  if (toys.optflags&FLAG_6) family = AF_INET6;
  else if (toys.optflags&FLAG_4) family = AF_INET;
  else family = 0;

  // If -I src_addr look it up. Allow numeric address of correct type.
  memset(&src_addr, 0, sizeof(src_addr));
  if (TT.I) {
    if (!(toys.optflags&FLAG_6) && inet_pton(AF_INET, TT.I,
      (void *)&src_addr.in.sin_addr))
        family = AF_INET;
    else if (!(toys.optflags&FLAG_4) && inet_pton(AF_INET6, TT.I,
      (void *)&src_addr.in6.sin6_addr))
        family = AF_INET6;
    else if (getifaddrs(&ifa2)) perror_exit("getifaddrs");
  }

  // Look up HOST address, filtering for correct type and interface.
  // If -I but no -46 then find compatible type between -I and HOST
  ai2 = xgetaddrinfo(*toys.optargs, 0, family, 0, 0, 0);
  for (ai = ai2; ai; ai = ai->ai_next) {

    // correct type?
    if (family && family!=ai->ai_family) continue;
    if (ai->ai_family!=AF_INET && ai->ai_family!=AF_INET6) continue;

    // correct interface?
    if (!TT.I || !ifa2) break;
    for (ifa = ifa2; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family!=ai->ai_family
          || strcmp(ifa->ifa_name, TT.I)) continue;
      sa = (void *)ifa->ifa_addr;

      break;
    }
    if (ifa) break;
  }

  if (!ai)
    error_exit("no v%d addr for -I %s", 4+2*(family==AF_INET6), TT.I);

  // Open DGRAM socket
  sa->sa_family = ai->ai_family;
  TT.sock = xsocket(ai->ai_family, SOCK_DGRAM,
    (ai->ai_family == AF_INET) ? IPPROTO_ICMP : IPPROTO_ICMPV6);
  if (TT.I && bind(TT.sock, sa, sizeof(src_addr))) perror_exit("bind");

  if (TT.t) {
    len = TT.t;

    if (ai->ai_family == AF_INET)
      setsockopt(TT.sock, IPPROTO_IP, IP_TTL, &len, 4);
    else setsockopt(TT.sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &len, 4);
  }

  if (!(toys.optflags&FLAG_q)) {
    printf("Ping %s (%s)", *toys.optargs, ntop(ai->ai_addr));
    if (TT.I) {
      *toybuf = 0;
      printf(" from %s (%s)", TT.I, ntop(sa));
    }
    // 20 byte TCP header, 8 byte ICMP header, plus data payload
    printf(": %ld(%ld) bytes.\n", TT.s, TT.s+28);
  }
  toys.exitval = 1;

  tW = tw = 0;
  tnext = millitime();
  if (TT.w) tw = TT.w*1000+tnext;

  // Send/receive packets
  for (;;) {
    int waitms = INT_MAX;

    // Exit due to timeout? (TODO: timeout is after last packet, waiting if
    // any packets ever dropped. Not timeout since packet was dropped.)
    tnow = millitime();
    if (tW) if (0>=(waitms = tW-tnow) || !sent) break;
    if (tw) {
      if (tnow>tw) break;
      else if (waitms>tw-tnow) waitms = tw-tnow;
    // Time to send the next packet?
    } else if (tnext-tnow <= 0) {
      tnext += TT.i_ms;

      memset(ih, 0, sizeof(*ih));
      ih->type = (ai->ai_family == AF_INET) ? 8 : 128;
      ih->un.echo.id = getpid();
      ih->un.echo.sequence = ++seq;
      if (TT.s >= 4) *(unsigned *)(ih+1) = tnow;

      ih->checksum = 0;
      ih->checksum = pingchksum((void *)toybuf, TT.s+sizeof(*ih));
      xsendto(TT.sock, toybuf, TT.s+sizeof(*ih), ai->ai_addr);
      sent++;
      if (toys.optflags&FLAG_f) printf(".");

      // last packet?
      if (TT.c) if (!--TT.c) {
        if (!TT.W) break;
        tW = tnow + TT.W*1000;
      }
    }

    // This is down here so it's against new period if we just sent a packet
    if (!tw && waitms>tnext-tnow) waitms = tnext-tnow;

    // wait for next packet or timeout

    if (waitms<0) waitms = 0;
    pfd.fd = TT.sock;
    pfd.events = POLLIN;
    if (0>(len = poll(&pfd, 1, waitms))) break;
    if (!len) continue;

    len = sizeof(src_addr2);
    len = recvfrom(TT.sock, toybuf, sizeof(toybuf), 0, sa2, (void *)&len);
    sent--;

    // reply id == 0 for ipv4, 129 for ipv6

    if (!(toys.optflags&FLAG_q)) {
      printf("%d bytes from %s: icmp_seq=%d ttl=%d", len, ntop(sa2),
             ih->un.echo.sequence, 0);
      if (len >= sizeof(*ih)+4) {
        unsigned lunchtime = millitime()-*(unsigned *)(ih+1);

        printf(" time=%u.%03u", lunchtime/1000, lunchtime%1000);
      }
      xputc('\n');
    }

    toys.exitval = 0;
  }

  if (CFG_TOYBOX_FREE) {
    freeaddrinfo(ai2);
    if (ifa2) freeifaddrs(ifa2);
  }
}
