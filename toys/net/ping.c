/* ping.c - check network connectivity
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.
 *
 * Note: ping_group_range should never have existed. To disable it, do:
 *   echo 0 999999999 > /proc/sys/net/ipv4/ping_group_range
 * (Android does this by default in its init script.)
 *
 * Yes, I wimped out and capped -s at sizeof(toybuf), waiting for a complaint...

// -s > 4088 = sizeof(toybuf)-sizeof(struct icmphdr), then kernel adds 20 bytes
USE_PING(NEWTOY(ping, "<1>1m#t#<0>255=64c#<0=3s#<0>4088=56i%W#<0=3w#<0qf46I:[-46]", TOYFLAG_USR|TOYFLAG_BIN))
USE_PING(OLDTOY(ping6, ping, TOYFLAG_USR|TOYFLAG_BIN))
 
config PING
  bool "ping"
  default y
  help
    usage: ping [OPTIONS] HOST

    Check network connectivity by sending packets to a host and reporting
    its response.

    Send ICMP ECHO_REQUEST packets to ipv4 or ipv6 addresses and prints each
    echo it receives back, with round trip time. Returns true if host alive.

    Options:
    -4, -6		Force IPv4 or IPv6
    -c CNT		Send CNT many packets (default 3, 0 = infinite)
    -f		Flood (print . and \b to show drops, default -c 15 -i 0.2)
    -i TIME		Interval between packets (default 1, need root for < .2)
    -I IFACE/IP	Source interface or address
    -m MARK		Tag outgoing packets using SO_MARK
    -q		Quiet (stops after one returns true if host is alive)
    -s SIZE		Data SIZE in bytes (default 56)
    -t TTL		Set Time To Live (number of hops)
    -W SEC		Seconds to wait for response after last -c packet (default 3)
    -w SEC		Exit after this many seconds
*/

#define FOR_ping 
#include "toys.h"

#include <ifaddrs.h>
#include <netinet/ip_icmp.h>

GLOBALS(
  char *I;
  long w, W, i, s, c, t, m;

  struct sockaddr *sa;
  int sock;
  unsigned long sent, recv, fugit, min, max;
)

// Print a summary. Called as a single handler or at exit.
static void summary(int sig)
{
  if (!FLAG(q) && TT.sent && TT.sa) {
    printf("\n--- %s ping statistics ---\n", ntop(TT.sa));
    printf("%lu packets transmitted, %lu received, %ld%% packet loss\n",
      TT.sent, TT.recv, ((TT.sent-TT.recv)*100)/(TT.sent?TT.sent:1));
    if (TT.recv)
      printf("round-trip min/avg/max = %lu/%lu/%lu ms\n",
        TT.min, TT.fugit/TT.recv, TT.max);
  }
  TT.sa = 0;
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
  struct icmphdr *ih = (void *)toybuf;
  union socksaddr srcaddr, srcaddr2;
  struct sockaddr *sa = (void *)&srcaddr;
  int family = 0, len;
  long long tnext, tW, tnow, tw;
  unsigned short seq = 0, pkttime;

  // Set nonstatic default values
  if (!FLAG(i)) TT.i = FLAG(f) ? 200 : 1000;
  else if (TT.i<200 && getuid()) error_exit("need root for -i <200");
  if (!FLAG(s)) TT.s = 56; // 64-PHDR_LEN
  if (FLAG(f) && !FLAG(c)) TT.c = 15;

  // ipv4 or ipv6? (0 = autodetect if -I or arg have only one address type.)
  if (FLAG(6) || strchr(toys.which->name, '6')) family = AF_INET6;
  else if (FLAG(4)) family = AF_INET;
  else family = 0;

  // If -I srcaddr look it up. Allow numeric address of correct type.
  memset(&srcaddr, 0, sizeof(srcaddr));
  if (TT.I) {
    if (!FLAG(6) && inet_pton(AF_INET, TT.I, (void *)&srcaddr.in.sin_addr))
      family = AF_INET;
    else if (!FLAG(4) && inet_pton(AF_INET6, TT.I, (void *)&srcaddr.in6.sin6_addr))
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
  TT.sa = ai->ai_addr;

  // Open DGRAM socket
  sa->sa_family = ai->ai_family;
  TT.sock = socket(ai->ai_family, SOCK_DGRAM,
    len = (ai->ai_family == AF_INET) ? IPPROTO_ICMP : IPPROTO_ICMPV6);
  if (TT.sock == -1) {
    perror_msg("socket SOCK_DGRAM %x", len);
    if (errno == EACCES) {
      fprintf(stderr, "Kernel bug workaround:\n"
        "echo 0 99999999 | sudo tee /proc/sys/net/ipv4/ping_group_range\n");
    }
    xexit();
  }
  if (TT.I) xbind(TT.sock, sa, sizeof(srcaddr));

  if (FLAG(m)) {
    len = TT.m;
    xsetsockopt(TT.sock, SOL_SOCKET, SO_MARK, &len, 4);
  }

  if (TT.t) {
    len = TT.t;
    if (ai->ai_family == AF_INET)
      xsetsockopt(TT.sock, IPPROTO_IP, IP_TTL, &len, 4);
    else xsetsockopt(TT.sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &len, 4);
  }

  if (!FLAG(q)) {
    printf("Ping %s (%s)", *toys.optargs, ntop(TT.sa));
    if (TT.I) {
      *toybuf = 0;
      printf(" from %s (%s)", TT.I, ntop(sa));
    }
    // 20 byte TCP header, 8 byte ICMP header, plus data payload
    printf(": %ld(%ld) bytes.\n", TT.s, TT.s+28);
  }
  TT.min = ULONG_MAX;
  toys.exitval = 1;

  tW = tw = 0;
  tnext = millitime();
  if (TT.w) tw = TT.w*1000+tnext;

  sigatexit(summary);

  // Send/receive packets
  for (;;) {
    int waitms = INT_MAX;

    // Exit due to timeout? (TODO: timeout is after last packet, waiting if
    // any packets ever dropped. Not timeout since packet was dropped.)
    tnow = millitime();
    if (tW) {
      if (0>=(waitms = tW-tnow) || !(TT.sent-TT.recv)) break;
      waitms = tW-tnow;
    }
    if (tw) {
      if (tnow>tw) break;
      else if (waitms>tw-tnow) waitms = tw-tnow;
    }

    // Time to send the next packet?
    if (!tW && tnext-tnow <= 0) {
      tnext += TT.i;

      memset(ih, 0, sizeof(*ih));
      ih->type = (ai->ai_family == AF_INET) ? 8 : 128;
      ih->un.echo.id = getpid();
      ih->un.echo.sequence = ++seq;
      if (TT.s >= 4) *(unsigned *)(ih+1) = tnow;

      ih->checksum = pingchksum((void *)toybuf, TT.s+sizeof(*ih));
      xsendto(TT.sock, toybuf, TT.s+sizeof(*ih), TT.sa);
      TT.sent++;
      if (FLAG(f) && !FLAG(q)) xputc('.');

      // last packet?
      if (TT.c) if (!--TT.c) {
        tW = tnow + TT.W*1000;
        waitms = 1; // check for immediate return even when W=0
      }
    }

    // This is down here so it's against new period if we just sent a packet
    if (!tW && waitms>tnext-tnow) waitms = tnext-tnow;

    // wait for next packet or timeout

    if (waitms<0) waitms = 0;
    if (!(len = xrecvwait(TT.sock, toybuf, sizeof(toybuf), &srcaddr2, waitms)))
      continue;

    TT.recv++;
    TT.fugit += (pkttime = millitime()-*(unsigned *)(ih+1));
    if (pkttime < TT.min) TT.min = pkttime;
    if (pkttime > TT.max) TT.max = pkttime;

    // reply id == 0 for ipv4, 129 for ipv6

    if (!FLAG(q)) {
      if (FLAG(f)) xputc('\b');
      else {
        printf("%d bytes from %s: icmp_seq=%d ttl=%d", len, ntop(&srcaddr2.s),
               ih->un.echo.sequence, 0);
        if (len >= sizeof(*ih)+4) printf(" time=%u ms", pkttime);
        xputc('\n');
      }
    }

    toys.exitval = 0;
  }

  sigatexit(0);
  summary(0);

  if (CFG_TOYBOX_FREE) {
    freeaddrinfo(ai2);
    if (ifa2) freeifaddrs(ifa2);
  }
}
