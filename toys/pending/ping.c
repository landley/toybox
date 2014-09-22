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

void ping_main(void)
{
  int family, protocol;
  union {
    struct in_addr in;
    struct in6_addr in6;
  } src_addr;
  char *host = 0;

  // Determine IPv4 vs IPv6 type

  if(!(toys.optflags & (FLAG_4|FLAG_6))) {
// todo getaddrinfo instead?
    if (inet_pton(AF_INET6, toys.optargs[0], (void*)&src_addr))
      toys.optflags |= FLAG_6;
  }

  if (toys.optflags & FLAG_6) {
    family = AF_INET6;
    protocol = IPPROTO_ICMPV6;
  } else {
    family = AF_INET;
    protocol = IPPROTO_ICMP;
  }

  if (!(toys.optflags & FLAG_s)) TT.size = 56; // 64-PHDR_LEN

  if (TT.iface) {
    memset(&src_addr, 0, sizeof(src_addr));

    // IP address?
    if (!inet_pton(family, TT.iface, &src_addr)) {
      struct ifaddrs *ifsave, *ifa = 0;

      // Interface name?
      if (!getifaddrs(&ifsave)) {
        for (ifa = ifsave; ifa; ifa = ifa->ifa_next) {
          if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != family) continue;
          if (!strcmp(ifa->ifa_name, TT.iface)) {
            if (family == AF_INET)
              memcpy(&src_addr,
                &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                sizeof(struct in_addr));
            else memcpy(&src_addr,
                &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
                sizeof(struct in6_addr));
            break;
          }
        }
        freeifaddrs(ifsave);
      }
      if (!ifa)
        error_exit("no v%d addr for -I %s", 4+2*(family==AF_INET6), TT.iface);
    }
    inet_ntop(family, &src_addr, toybuf, sizeof(toybuf));
    host = xstrdup(toybuf);
  }

printf("host=%s\n", host);

  // Open raw socket
  TT.sock = xsocket(family, SOCK_RAW, protocol);
}
