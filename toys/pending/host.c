/* host.c - DNS lookup utility
 *
 * Copyright 2014 Rich Felker <dalias@aerifal.cx>
 *
 * No standard, but there's a version in bind9

USE_HOST(NEWTOY(host, "<1>2avt:", TOYFLAG_USR|TOYFLAG_BIN))

config HOST
  bool "host"
  default n
  help
    usage: host [-av] [-t TYPE] NAME [SERVER]

    Perform DNS lookup on NAME, which can be a domain name to lookup,
    or an ipv4 dotted or ipv6 colon seprated address to reverse lookup.
    SERVER (if present) is the DNS server to use.

    -a	no idea
    -t	not a clue
    -v	verbose
*/

#define FOR_host
#include "toys.h"

GLOBALS(
  char *type_str;
)

#include <resolv.h>

#define PL_IP 1
#define PL_NAME 2
#define PL_DATA 3
#define PL_TEXT 4
#define PL_SOA 5
#define PL_MX 6
#define PL_SRV 7

static const struct rrt {
  const char *name;
  const char *msg;
  int pl;
  int af;
} rrt[] = {
  [1] = { "A", "has address", PL_IP, AF_INET },
  [28] = { "AAAA", "has address", PL_IP, AF_INET6 },
  [2] = { "NS", "name server", PL_NAME },
  [5] = { "CNAME", "is a nickname for", PL_NAME },
  [16] = { "TXT", "descriptive text", PL_TEXT },
  [6] = { "SOA", "start of authority", PL_SOA },
  [12] = { "PTR", "domain name pointer", PL_NAME },
  [15] = { "MX", "mail is handled", PL_MX },
  [33] = { "SRV", "mail is handled", PL_SRV },
  [255] = { "*", 0, 0 },
};

static const char rct[16][32] = {
  "Success",
  "Format error",
  "Server failure",
  "Non-existant domain",
  "Not implemented",
  "Refused",
};

void host_main(void)
{
  int verbose=(toys.optflags & (FLAG_a|FLAG_v)), type;
  char *name, *nsname;
  int i, j, ret, sec, count, rcode, qlen, alen, pllen = 0;
  unsigned ttl, pri, v[5];
  unsigned char qbuf[280], abuf[512], *p;
  char rrname[256], plname[640], ptrbuf[64];
  struct addrinfo *ai, iplit_hints = { .ai_flags = AI_NUMERICHOST };

  name = *toys.optargs;
  nsname = toys.optargs[1];

  if (!TT.type_str && (toys.optflags & FLAG_a)) TT.type_str = "255";
  if (!getaddrinfo(name, 0, &iplit_hints, &ai)) {
    switch (ai->ai_family) {
      unsigned char *a;
      static const char xdigits[] = "0123456789abcdef";
    case AF_INET:
      a = (void *)&((struct sockaddr_in *)ai->ai_addr)->sin_addr;
      snprintf(ptrbuf, sizeof ptrbuf,
        "%d.%d.%d.%d.in-addr.arpa",
        a[3], a[2], a[1], a[0]);
      break;
    case AF_INET6:
      a = (void *)&((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
      for (j=0, i=15; i>=0; i--) {
        ptrbuf[j++] = xdigits[a[i]&15];
        ptrbuf[j++] = '.';
        ptrbuf[j++] = xdigits[a[i]>>4];
        ptrbuf[j++] = '.';
      }
      strcpy(ptrbuf+j, "ip6.arpa");
      break;
    }
    name = ptrbuf;
    if (!TT.type_str) TT.type_str="12";
  } else {
    if (!TT.type_str) TT.type_str="1";
  }

  if (TT.type_str[0]-'0' < 10u) {
    type = atoi(TT.type_str);
  } else {
    type = -1;
    for (i=0; i < sizeof rrt / sizeof *rrt; i++) {
      if (rrt[i].name && !strcasecmp(TT.type_str, rrt[i].name)) {
        type = i;
        break;
      }
    }
    if (!strcasecmp(TT.type_str, "any")) type = 255;
    if (type < 0) {
      fprintf(stderr, "Invalid query type: %s\n", TT.type_str);
      goto out_error;
    }
  }

  qlen = res_mkquery(0, name, 1, type, 0, 0, 0, qbuf, sizeof qbuf);
  if (qlen < 0) {
    fprintf(stderr, "Invalid query parameters: %s", name);
    goto out_error;
  }

  if (nsname) {
    struct addrinfo ns_hints = { .ai_socktype = SOCK_DGRAM };
    if ((ret = getaddrinfo(nsname, "53", &ns_hints, &ai)) < 0) {
      fprintf(stderr, "Error looking up server name: %s\n",
        gai_strerror(ret));
      goto out_error;
    }
    int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s < 0 || connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
      fprintf(stderr, "Socket error: %s\n", strerror(errno));
      goto out_error;
    }
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
      &(struct timeval){ .tv_sec = 5 },
      sizeof (struct timeval));
    printf("Using domain server %s:\n", nsname);
    send(s, qbuf, qlen, 0);
    alen = recv(s, abuf, sizeof abuf, 0);
  } else {
    alen = res_send(qbuf, qlen, abuf, sizeof abuf);
  }

  if (alen < 12) {
    fprintf(stderr, "Host not found, try again.\n");
    goto out_error;
  }

  rcode = abuf[3] & 15;

  if (verbose) {
    printf("rcode = %d (%s), ancount = %d\n",
      rcode, rct[rcode], 256*abuf[6] + abuf[7]);
    if (!(abuf[2] & 4))
      printf("The following answer is not authoritative:\n");
  }

  if (rcode) {
    fprintf(stderr, "Host not found.\n");
    if (!verbose) goto out_error;
  }

  p = abuf + 12;
  for (sec=0; sec<4; sec++) {
    count = 256*abuf[4+2*sec] + abuf[5+2*sec];
    if (verbose && count>0 && sec>1) {
      puts(sec==2 ? "For authoritative answers, see:"
        : "Additional information:");
    }
    for (; count--; p += pllen) {
      p += dn_expand(abuf, abuf+alen, p, rrname, sizeof rrname);
      type = 256*p[0] + p[1];
      p += 4;
      if (!sec) continue;
      ttl = 16777216*p[0] + 65536*p[1] + 256*p[2] + p[3];
      p += 4;
      pllen = 256*p[0] + p[1];
      p += 2;
      switch (type<sizeof rrt/sizeof *rrt ? rrt[type].pl : 0) {
      case PL_IP:
        inet_ntop(rrt[type].af, p, plname, sizeof plname);
        break;
      case PL_NAME:
        dn_expand(abuf, abuf+alen, p, plname, sizeof plname);
        break;
      case PL_TEXT:
        snprintf(plname, sizeof plname, "\"%.*s\"", pllen, p);
        break;
      case PL_SOA:
        i = dn_expand(abuf, abuf+alen, p, plname, sizeof plname - 1);
        strcat(plname, " ");
        i += dn_expand(abuf, abuf+alen, p+i, plname+strlen(plname),
          sizeof plname-strlen(plname));
        for (j=0; j<5; j++)
          v[j] = 16777216u*p[i+4*j] + 65536*p[1+i+4*j]
            + 256*p[2+i+4*j] + p[3+i+4*j];
        snprintf(plname+strlen(plname),
          sizeof plname-strlen(plname),
          "(\n\t\t%u\t;serial (version)\n"
          "\t\t%u\t;refresh period\n"
          "\t\t%u\t;retry interval\n"
          "\t\t%u\t;expire time\n"
          "\t\t%u\t;default ttl\n"
          "\t\t)", v[0], v[1], v[2], v[3], v[4]);
        break;
      case PL_MX:
        pri = 256*p[0] + p[1];
        snprintf(plname, sizeof plname,
          verbose ? "%d " : "(pri=%d) by ", pri);
        dn_expand(abuf, abuf+alen, p+2,
          plname+strlen(plname),
          sizeof plname - strlen(plname));
        break;
      case PL_SRV:
        for (j=0; j<3; j++)
          v[j] = 256*p[2*j] + p[1+2*j];
        snprintf(plname, sizeof plname,
          "%u %u %u ", v[0], v[1], v[2]);
        dn_expand(abuf, abuf+alen, p+6,
          plname+strlen(plname),
          sizeof plname - strlen(plname));
        break;
      default:
        printf("%s unsupported RR type %u\n", rrname, type);
        continue;
      }
      if (verbose) {
        printf("%s\t%u\t%s %s\t%s\n",
          rrname, ttl, "IN", rrt[type].name, plname);
      } else if (rrt[type].msg) {
        printf("%s %s %s\n", rrname, rrt[type].msg, plname);
      }
    }
    if (!verbose && sec==1) break;
  }
  toys.exitval = rcode;
  return;

out_error:
  toys.exitval = 1;
}
