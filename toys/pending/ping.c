/* ping.c - check network connectivity
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.
 
USE_PING(NEWTOY(ping, "<1>1t#<0>255c#<0s#<0>65535I:W#<0w#<0q46[-46]", TOYFLAG_ROOTONLY|TOYFLAG_USR|TOYFLAG_BIN))
 
config PING
  bool "ping"
  default n
  help
    usage: ping [OPTIONS] HOST

    Check network connectivity by sending packets to a host and reporting
    its response.

    Send ICMP ECHO_REQUEST packets to ipv4 or ipv6 addresses and prints each
    echo it receives back, with round trip time.

    Options:
    -4, -6      Force IPv4 or IPv6
    -c CNT      Send CNT many packets
    -I IFACE/IP Source interface or address
    -q          Quiet, only displays output at start and when finished
    -s SIZE     Packet SIZE in bytes (default 56)
    -t TTL      Set Time (number of hops) To Live
    -W SEC      Seconds to wait for response after all packets sent (default 10)
    -w SEC      Exit after this many seconds
*/

#define FOR_ping 
#include "toys.h"

#include <ifaddrs.h>

GLOBALS(
  long wait_exit;
  long wait_resp;
  char *iface;
  long size;
  long count;
  long ttl;

  int sock;
)

void *sock2addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET)
    return &((struct sockaddr_in *)sa)->sin_addr;
  return &((struct sockaddr_in6 *)sa)->sin6_addr;
}

void ping_main(void)
{
  struct addrinfo *ai, *ai2;
  struct ifaddrs *ifa, *ifa2 = 0;
  union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } src_addr;
  struct sockaddr *sa = (void *)&src_addr;
  int family = 0;

  // no 4/6 specified: -I has only one, arg must match
  // no 4/6 specified: arg is one, -I must match
  // 4/6 specified, both must match

  if (!(toys.optflags&FLAG_s)) TT.size = 56; // 64-PHDR_LEN
  if (toys.optflags&FLAG_6) family = AF_INET6;
  else if (toys.optflags&FLAG_4) family = AF_INET;
  else family = 0;

  // If -I src_addr look it up. Allow numeric address of correct type.
  memset(&src_addr, 0, sizeof(src_addr));
  if (TT.iface) {
    if (!(toys.optflags&FLAG_6) && inet_pton(AF_INET, TT.iface,
      (void *)&src_addr.in.sin_addr))
        family = sa->sa_family = AF_INET;
    else if (!(toys.optflags&FLAG_4) && inet_pton(AF_INET6, TT.iface,
      (void *)&src_addr.in6.sin6_addr))
        family = sa->sa_family = AF_INET6;
    else if (getifaddrs(&ifa2)) perror_exit("getifaddrs");
  }

  // Look up HOST address, filtering for correct type.
  // If -I but no -46 then find compatible type between -I and HOST
  ai2 = xgetaddrinfo(toys.optargs[0], 0, family, 0, 0, 0);
  for (ai = ai2; ai; ai = ai->ai_next) {
    if (family && family!=ai->ai_family) continue;
    if (ai->ai_family!=AF_INET && ai->ai_family!=AF_INET6) continue;
    if (!TT.iface || !ifa2) break;
    for (ifa = ifa2; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family!=ai->ai_family
          || strcmp(ifa->ifa_name, TT.iface)) continue;
      sa = (void *)ifa->ifa_addr;

      break;
    }
    if (ifa) break;
  }

  if (!ai)
    error_exit("no v%d addr for -I %s", 4+2*(family==AF_INET6), TT.iface);

  inet_ntop(family, sock2addr(sa), toybuf, sizeof(toybuf));
  printf("host=%s\n", toybuf);
  *toybuf = 0;
  inet_ntop(ai->ai_family, sock2addr(ai->ai_addr), toybuf, sizeof(toybuf));
  printf("targ=%s\n", toybuf);

  // Open raw socket
  TT.sock = xsocket(ai->ai_family, SOCK_DGRAM, (ifa->ifa_addr->sa_family == AF_INET) ?
    IPPROTO_ICMP : IPPROTO_ICMPV6);
  if (TT.iface && bind(TT.sock, ifa->ifa_addr, sizeof(src_addr)))
    perror_exit("bind");

  if (CFG_TOYBOX_FREE) {
    freeaddrinfo(ai2);
    if (ifa2) freeifaddrs(ifa2);
  }
}
