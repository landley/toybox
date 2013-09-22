/* traceroute - trace the route to "host".
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_TRACEROUTE(NEWTOY(traceroute, "<1>2f#<1>255=1z#<0>86400=0g*w#<0>86400=5t#<0>255=0s:q#<1>255=3p#<1>65535=33434m#<1>255=30rvndlIUF64", TOYFLAG_USR|TOYFLAG_BIN))

config TRACEROUTE
  bool "traceroute"
  default n
  help
    usage: traceroute [-FUIldnvr] [-f 1ST_TTL] [-m MAXTTL] [-p PORT] [-q PROBES]
    [-s SRC_IP] [-t TOS] [-w WAIT_SEC] [-g GATEWAY] [-i IFACE] [-z PAUSE_MSEC] HOST [BYTES]

    Trace the route to HOST

    -F    Set the don't fragment bit
    -U    Use UDP datagrams instead of ICMP ECHO
    -I    Use ICMP ECHO instead of UDP datagrams
    -l    Display the TTL value of the returned packet
    -f    Start from the 1ST_TTL hop (instead from 1)(RANGE 1 to 255)
    -d    Set SO_DEBUG options to socket
    -n    Print numeric addresses
    -v    verbose
    -r    Bypass routing tables, send directly to HOST
    -m    Max time-to-live (max number of hops)(RANGE 1 to 255)
    -p    Base UDP port number used in probes(default 33434)(RANGE 1 to 65535)
    -q    Number of probes per TTL (default 3)(RANGE 1 to 255)
    -s    IP address to use as the source address
    -t    Type-of-service in probe packets (default 0)(RANGE 0 to 255)
    -w    Time in seconds to wait for a response (default 3)(RANGE 0 to 86400)
    -g    Loose source route gateway (8 max)
    -z    Pause Time in milisec (default 0)(RANGE 0 to 86400)
*/
#define FOR_traceroute
#include "toys.h"
#include "toynet.h"
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

GLOBALS(
  long max_ttl;
  long port;
  long ttl_probes;
  char *src_ip;
  long tos;
  long wait_time;
  struct arg_list *loose_source;
  long pause_time;
  long first_ttl;
  int recv_sock;
  int snd_sock;
  unsigned msg_len;
  struct ip *packet;
  uint32_t gw_list[9];
  uint32_t ident;
)


#define ICMP_HD_SIZE    8
#define send_icmp      ((struct icmp *)(TT.packet +   1))
#define send_udp      ((struct udphdr *)(TT.packet + 1))

struct payload_s {
  unsigned char seq;
  unsigned char ttl;
  struct timeval tv; 
};

static struct payload_s *send_data;
static struct sockaddr_in dest, from;

// Computes and returns checksum SUM of buffer P of length LEN
static u_int16_t in_cksum(u_int16_t *p, u_int len)
{
  u_int32_t sum = 0;
  int nwords = len >> 1;

  while (nwords-- != 0) sum += *p++;
  if (len & 1) {
    union {
      u_int16_t w;
      u_int8_t c[2];
    } u;
    u.c[0] = *(u_char *) p;
    u.c[1] = 0;
    sum += u.w;
  }
  // end-around-carry 
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  return (~sum);
}

/*
 * sends a single probe packet with sequence SEQ and
 * time-to-live TTL
 */
static void send_probe(int seq, int ttl)
{
  int res, len;
  void *out;

  if (toys.optflags & FLAG_U) {
    send_data->seq = seq;
    send_data->ttl = ttl;
    dest.sin_port = TT.port + seq;
  } else {
    send_icmp->icmp_seq = htons(seq);
    send_icmp->icmp_cksum = 0;
    send_icmp->icmp_cksum = in_cksum((uint16_t *) send_icmp, TT.msg_len - sizeof(struct ip));
    if (send_icmp->icmp_cksum == 0) send_icmp->icmp_cksum = 0xffff;
  }

  res = setsockopt(TT.snd_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
  if (res < 0) perror_exit("setsockopt ttl %d", ttl);

  if (toys.optflags & FLAG_U) {
    out = send_data;
    len = sizeof(struct payload_s);
  } else {
    out = send_icmp;
    len = TT.msg_len - sizeof(struct ip);
  }
  res = sendto(TT.snd_sock, out, len, 0, (struct sockaddr *) &dest, sizeof(dest));
  if (res != len) perror_exit(" sendto");
}

static void resolve_addr(char *host, int type, int proto, struct sockaddr_in *sock)
{
  struct addrinfo *rp, *info, hint;
  int ret;

  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET;
  hint.ai_socktype = type;
  hint.ai_protocol = proto;

  ret = getaddrinfo(host, NULL, &hint, &info);
  if (ret || !info) perror_exit("bad address:  %s ", host);

  for (rp = info; rp; rp = rp->ai_next) {  
    if (rp->ai_addrlen == sizeof(struct sockaddr_in)) {
      memcpy(sock, rp->ai_addr, rp->ai_addrlen);
      break;                               
    }
  }
  freeaddrinfo(info);                      
  if (!rp) perror_exit("Resolve failed ");
}

static void do_trace()
{
  int seq, fexit, ttl, tv = TT.wait_time * 1000;
  struct pollfd pfd[1];

  pfd[0].fd = TT.recv_sock;
  pfd[0].events = POLLIN;
  for (ttl = TT.first_ttl; ttl <= TT.max_ttl; ++ttl) {
    int probe, dest_reach = 0;
    struct timeval t1, t2;
    struct sockaddr_in last;

    memset(&last, 0, sizeof(last));
    fexit = 0;
    printf("%2d", ttl);

    for (probe = 0, seq = 0; probe < TT.ttl_probes; ++probe) {
      int res = 0, tleft;

      fflush(NULL);
      if (probe && (toys.optflags & FLAG_z)) usleep(TT.pause_time * 1000);

      send_probe(++seq, ttl);
      gettimeofday(&t1, NULL);
      t2 = t1;

      while ((tleft = (int)(tv - ((unsigned long long)(t2.tv_sec * 1000ULL 
                  + t2.tv_usec/1000) - (unsigned long long)(t1.tv_sec * 1000ULL
                    + t1.tv_usec/1000)))) >= 0) {
        if (!(res = poll(pfd, 1, tleft))) {                         
          printf("  *");                        
          break;
        }
        gettimeofday(&t2, NULL);
        if (res < 0) {
          if (errno != EINTR) perror_exit("poll");
          continue;
        }

        if (pfd[0].revents) {
          unsigned addrlen = sizeof(from);
          struct ip *rcv_pkt = (struct ip*) toybuf;
          int rcv_len, pmtu = 0;

          rcv_len = recvfrom(TT.recv_sock, rcv_pkt, sizeof(toybuf),
              MSG_DONTWAIT, (struct sockaddr *) &from, &addrlen);
          if (rcv_len <= 0) continue;
          else {
            struct icmp *ricmp;
            int icmp_res = 0;

            ricmp = (struct icmp *) ((void*)rcv_pkt + (rcv_pkt->ip_hl << 2));
            if (ricmp->icmp_code == ICMP_UNREACH_NEEDFRAG)
              pmtu = ntohs(ricmp->icmp_nextmtu);

            if ((ricmp->icmp_type == ICMP_TIMXCEED
                  && ricmp->icmp_code == ICMP_TIMXCEED_INTRANS)
                || ricmp->icmp_type == ICMP_UNREACH
                || ricmp->icmp_type == ICMP_ECHOREPLY) {

              struct ip *hip;
              struct udphdr *hudp;
              struct icmp *hicmp;

              hip = &ricmp->icmp_ip;
              if (toys.optflags & FLAG_U) {
                hudp = (struct udphdr*) (hip + (hip->ip_hl << 2));
                if ((hip->ip_hl << 2) + 12 <= (rcv_len - (rcv_pkt->ip_hl << 2))
                    && hip->ip_p == IPPROTO_UDP
                    && hudp->dest == htons(TT.port + seq))
                  icmp_res = (ricmp->icmp_type == ICMP_TIMXCEED ?-1 : ricmp->icmp_code);
              } else {
                hicmp = (struct icmp *) ((void*)hip + (hip->ip_hl << 2));
                if (ricmp->icmp_type == ICMP_ECHOREPLY && ricmp->icmp_id == htons(TT.ident)
                    && ricmp->icmp_seq == htons(seq))
                  icmp_res = ICMP_UNREACH_PORT;

                if ((hip->ip_hl << 2) + ICMP_HD_SIZE <= (rcv_len - (rcv_pkt->ip_hl << 2))
                    && hip->ip_p == IPPROTO_ICMP
                    && hicmp->icmp_id == htons(TT.ident)
                    && hicmp->icmp_seq == htons(seq))
                  icmp_res = (ricmp->icmp_type == ICMP_TIMXCEED ? -1 : ricmp->icmp_code);
              }
            }
            if (!icmp_res) continue;

            if (addrlen > 0) {
              unsigned delta = (t2.tv_sec * 1000000ULL + t2.tv_usec)
                - (t1.tv_sec * 1000000ULL + t1.tv_usec);

              if (memcmp(&last.sin_addr, &from.sin_addr, sizeof(struct in_addr))) {
                if (!(toys.optflags & FLAG_n)) {
                  char host[NI_MAXHOST];
                  if (!getnameinfo((struct sockaddr *) &from,
                        sizeof(from), host, NI_MAXHOST, NULL, 0, 0))
                    printf("  %s (", host);
                  else printf(" %s (", inet_ntoa(from.sin_addr));
                }
                printf(" %s", inet_ntoa(from.sin_addr));
                if (!(toys.optflags & FLAG_n) ) printf(")");
                last = from;
              }
              printf("  %u.%03u ms", delta / 1000, delta % 1000);
              if (toys.optflags & FLAG_l) printf(" (%d)", rcv_pkt->ip_ttl);
              if (toys.optflags & FLAG_v) {
                printf(" %d bytes from %s : icmp type %d code %d\t",
                    rcv_len, inet_ntoa(from.sin_addr),
                    ricmp->icmp_type, ricmp->icmp_code);
              }
            } else printf("\t!H");

            switch (icmp_res) {
              case ICMP_UNREACH_PORT:
                if (rcv_pkt->ip_ttl <= 1) printf(" !");
                dest_reach = 1;
                break;
              case ICMP_UNREACH_NET:
                printf(" !N");
                ++fexit;
                break;
              case ICMP_UNREACH_HOST:
                printf(" !H");
                ++fexit;
                break;
              case ICMP_UNREACH_PROTOCOL:
                printf(" !P");
                dest_reach = 1;
                break;
              case ICMP_UNREACH_NEEDFRAG:
                printf(" !F-%d", pmtu);
                ++fexit;
                break;
              case ICMP_UNREACH_SRCFAIL:
                printf(" !S");
                ++fexit;
                break;
              case ICMP_UNREACH_FILTER_PROHIB:
              case ICMP_UNREACH_NET_PROHIB:
                printf(" !A");
                ++fexit;
                break;
              case ICMP_UNREACH_HOST_PROHIB:
                printf(" !C");
                ++fexit;
                break;
              case ICMP_UNREACH_HOST_PRECEDENCE:
                printf(" !V");
                ++fexit;
                break;
              case ICMP_UNREACH_PRECEDENCE_CUTOFF:
                printf(" !C");
                ++fexit;
                break;
              case ICMP_UNREACH_NET_UNKNOWN:  
              case ICMP_UNREACH_HOST_UNKNOWN:
                printf(" !U");
                ++fexit;
                break;
              case ICMP_UNREACH_ISOLATED:
                printf(" !I");
                ++fexit;
                break;
              case ICMP_UNREACH_TOSNET:
              case ICMP_UNREACH_TOSHOST:
                printf(" !T");
                ++fexit;
                break;
            }
            break;
          }
        }
      }
    }
    xputc('\n');
    if (!memcmp(&from.sin_addr, &dest.sin_addr, sizeof(struct in_addr))
        || dest_reach || (fexit >= TT.ttl_probes -1))
      break;
  }

}

void traceroute_main(void)
{
  unsigned opt_len = 0, pack_size, tyser = 0;
  int set = 1, lsrr = 0;

  if (toys.optflags & FLAG_g) {
    struct arg_list *node;

    for (node = TT.loose_source; node; node = node->next, lsrr++) {
      struct sockaddr_in sin;

      memset( &sin, 0, sizeof(sin));
      if (lsrr >= 8) error_exit("no more than 8 gateways"); // NGATEWAYS
      resolve_addr(node->arg, SOCK_STREAM, 0, &sin);
      TT.gw_list[lsrr] = sin.sin_addr.s_addr;
    }
    opt_len = (lsrr + 1) * sizeof(TT.gw_list[0]);
  }

  pack_size = sizeof(struct ip) + opt_len;
  pack_size += (toys.optflags & FLAG_U) ? sizeof(struct udphdr) 
    + sizeof(struct payload_s) : ICMP_HD_SIZE;

  if (toys.optargs[1])
    TT.msg_len = get_int_value(toys.optargs[1], pack_size, 32768);//max packet size

  TT.msg_len = (TT.msg_len < pack_size) ? pack_size : TT.msg_len;
  TT.recv_sock = xsocket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (toys.optflags & FLAG_d
      && (setsockopt(TT.recv_sock, SOL_SOCKET, SO_DEBUG, &set, sizeof(set)) < 0))
    perror_exit("SO_DEBUG failed ");
  if (toys.optflags & FLAG_r
      && (setsockopt(TT.recv_sock, SOL_SOCKET, SO_DONTROUTE, &set, sizeof(set)) < 0))
    perror_exit("SO_DONTROUTE failed ");

  if (toys.optflags & FLAG_U) TT.snd_sock = xsocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  else TT.snd_sock = xsocket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

  resolve_addr(toys.optargs[0], ((toys.optflags & FLAG_U) ? SOCK_DGRAM : SOCK_RAW),
      ((toys.optflags & FLAG_U) ? IPPROTO_UDP : IPPROTO_ICMP), &dest);

  if (lsrr > 0) {
    unsigned char optlist[MAX_IPOPTLEN];
    unsigned size;

    TT.gw_list[lsrr] = dest.sin_addr.s_addr;
    ++lsrr;

    optlist[0] = IPOPT_NOP;
    optlist[1] = IPOPT_LSRR;// loose source route option 
    size = lsrr * sizeof(TT.gw_list[0]);
    optlist[2] = size + 3;
    optlist[3] = IPOPT_MINOFF;
    memcpy(optlist + 4, TT.gw_list, size);

    if (setsockopt(TT.snd_sock, IPPROTO_IP, IP_OPTIONS,
          (char *)optlist, size + sizeof(TT.gw_list[0])) < 0)
      perror_exit("LSRR IP_OPTIONS");
  }

  if (setsockopt(TT.snd_sock, SOL_SOCKET, SO_SNDBUF, &TT.msg_len, sizeof(TT.msg_len)) < 0)
    perror_exit("SO_SNDBUF failed ");

  if ((toys.optflags & FLAG_t) && 
      setsockopt(TT.snd_sock, IPPROTO_IP, IP_TOS, &tyser, sizeof(tyser)) < 0)
    perror_exit("IP_TOS %d failed ", TT.tos);

#ifdef IP_DONTFRAG
  if ((toys.optflags & FLAG_F) &&
      (setsockopt(TT.snd_sock, IPPROTO_IP, IP_DONTFRAG, &set, sizeof(set)) < 0))
    perror_exit("IP_DONTFRAG failed ");
#endif

  if ((toys.optflags & FLAG_d)
      && (setsockopt(TT.snd_sock, SOL_SOCKET, SO_DEBUG, &set, sizeof(set)) < 0))
    perror_exit("SO_DEBUG failed ");
  if ((toys.optflags & FLAG_r) &&
      (setsockopt(TT.snd_sock, SOL_SOCKET, SO_DONTROUTE, &set, sizeof(set)) < 0))
    perror_exit("SO_DONTROUTE failed ");


  TT.packet = xmalloc(TT.msg_len);
  TT.ident = getpid();

  if (toys.optflags & FLAG_U) send_data = (struct payload_s *) (send_udp + 1);
  else {
    TT.ident |= 0x8000;
    send_icmp->icmp_type = ICMP_ECHO;
    send_icmp->icmp_id = htons(TT.ident);
  }
  if (toys.optflags & FLAG_s) {
    struct sockaddr_in *source =  xzalloc(sizeof(struct sockaddr_in));
    inet_aton(TT.src_ip, &source->sin_addr);
    if (setsockopt(TT.snd_sock, IPPROTO_IP, IP_MULTICAST_IF, 
          (struct sockaddr*)source, sizeof(*source)))
      perror_exit("can't set multicast source interface");
    if (bind(TT.snd_sock,(struct sockaddr*)source, sizeof(*source)))
      perror_exit("bind");
    free(source);
  }

  if(TT.first_ttl > TT.max_ttl) 
    error_exit("ERROR :Range for -f is 1 to %d (max ttl)", TT.max_ttl);

  printf("traceroute to %s(%s)", toys.optargs[0], inet_ntoa(dest.sin_addr));
  if (toys.optflags & FLAG_s) printf(" from %s", TT.src_ip);
  printf(", %ld hops max, %u byte packets\n", TT.max_ttl, TT.msg_len);
  
  do_trace();
}
