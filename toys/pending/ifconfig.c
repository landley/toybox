/* ifconfig.c - Configure network interface.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2012 Kyungwan Han <asura321@gamil.com>
 * Reviewed by Kyungsu Kim <kaspyx@gmail.com>
 *
 * Not in SUSv4.

USE_IFCONFIG(NEWTOY(ifconfig, "?a", TOYFLAG_BIN))

config IFCONFIG
  bool "ifconfig"
  default y
  help
    usage: ifconfig [-a] interface [address]

    Configure network interface.

    [add ADDRESS[/PREFIXLEN]]
    [del ADDRESS[/PREFIXLEN]]
    [[-]broadcast [ADDRESS]] [[-]pointopoint [ADDRESS]]
    [netmask ADDRESS] [dstaddr ADDRESS]
    [outfill NN] [keepalive NN]
    [hw ether|infiniband ADDRESS] [metric NN] [mtu NN]
    [[-]trailers] [[-]arp] [[-]allmulti]
    [multicast] [[-]promisc] [txqueuelen NN] [[-]dynamic]
    [mem_start NN] [io_addr NN] [irq NN]
    [up|down] ...    
*/

#define FOR_ifconfig
#include "toys.h"

#include <net/if_arp.h>
#include <net/ethernet.h>

GLOBALS(
  int sockfd;
)

typedef struct sockaddr_with_len {
  union {
    struct sockaddr sock;
    struct sockaddr_in sock_in;
    struct sockaddr_in6 sock_in6;
  } sock_u;
} sockaddr_with_len;

//for the param settings.

//for ipv6 add/del
struct ifreq_inet6 {
  struct in6_addr ifrinte6_addr;
  uint32_t ifrinet6_prefixlen;
  int ifrinet6_ifindex;
};

/*
 * use to get the socket address with the given host ip.
 */
sockaddr_with_len *get_sockaddr(char *host, int port, sa_family_t af)
{
  sockaddr_with_len *swl = NULL;
  in_port_t port_num = htons(port);
  struct addrinfo hints, *result, *rp;
  int status;
  char *s;

  if (!strncmp(host, "local:", 6)) {
    struct sockaddr_un *sockun;

    swl = xzalloc(sizeof(struct sockaddr_with_len));
    swl->sock_u.sock.sa_family = AF_UNIX;
    sockun = (struct sockaddr_un *)&swl->sock_u.sock;
    xstrncpy(sockun->sun_path, host + 6, sizeof(sockun->sun_path));

    return swl;
  }

  // [ipv6]:port or exactly one :

  if (*host == '[') {
    host++;
    s = strchr(host, ']');
    if (s && !s[1]) s = 0;
    else {
      if (!s || s[1] != ':') error_exit("bad address '%s'", host-1);
      s++;
    }
  } else {
    s = strrchr(host, ':');
    if (strchr(host, ':') != s) s = 0;
  }

  if (s++) {
    char *ss;
    unsigned long p = strtoul(s, &ss, 0);
    if (*ss || p > 65535) error_exit("bad port '%s'", s);
    port = p;
  }

  memset(&hints, 0 , sizeof(struct addrinfo));
  hints.ai_family = af;
  hints.ai_socktype = SOCK_STREAM;

  status = getaddrinfo(host, NULL, &hints, &result);
  if (status) error_exit("bad address '%s' : %s", host, gai_strerror(status));

  for (rp = result; rp; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET || rp->ai_family == AF_INET6) {
      swl = xmalloc(sizeof(struct sockaddr_with_len));
      memcpy(&swl->sock_u.sock, rp->ai_addr, rp->ai_addrlen);
      break;
    }
  }
  freeaddrinfo(result);
  if (!rp) error_exit("bad host name");

  if(swl->sock_u.sock.sa_family == AF_INET)
    swl->sock_u.sock_in.sin_port = port_num;
  else if(swl->sock_u.sock.sa_family == AF_INET6)
    swl->sock_u.sock_in6.sin6_port = port_num;

  return swl;
}

static void set_address(char *host_name, struct ifreq *ifre, int request)
{
  struct sockaddr_in *sock_in = (struct sockaddr_in *)&ifre->ifr_addr;
  sockaddr_with_len *swl = NULL;

  memset(sock_in, 0, sizeof(struct sockaddr_in));
  sock_in->sin_family = AF_INET;

  //Default 0.0.0.0
  if(strcmp(host_name, "default") == 0) sock_in->sin_addr.s_addr = INADDR_ANY;
  else {
    swl = get_sockaddr(host_name, 0, AF_INET);
    sock_in->sin_addr = swl->sock_u.sock_in.sin_addr;
    free(swl);
  }
  xioctl(TT.sockfd, request, ifre);
}

static void display_ifconfig(char *name, int always, unsigned long long val[])
{
  struct ifreq ifre;
  struct {
    int type;
    char *title;
  } types[] = {
    {ARPHRD_LOOPBACK, "Local Loopback"}, {ARPHRD_ETHER, "Ethernet"},
    {ARPHRD_PPP, "Point-to-Point Protocol"}, {ARPHRD_INFINIBAND, "InfiniBand"},
    {ARPHRD_SIT, "IPv6-in-IPv4"}, {-1, "UNSPEC"}
  };
  int i;
  char *p;
  FILE *fp;
  short flags;

  xstrncpy(ifre.ifr_name, name, IFNAMSIZ);
  if (ioctl(TT.sockfd, SIOCGIFFLAGS, &ifre)<0) perror_exit("%s", name);
  flags = ifre.ifr_flags;
  if (!always && !(flags & IFF_UP)) return;

  // query hardware type and hardware address
  i = ioctl(TT.sockfd, SIOCGIFHWADDR, &ifre);

  for (i=0; i < (sizeof(types)/sizeof(*types))-1; i++)
    if (ifre.ifr_hwaddr.sa_family == types[i].type) break;

  xprintf("%-9s Link encap:%s  ", name, types[i].title);
  if(i >= 0 && ifre.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
    xprintf("HWaddr ");
    for (i=0; i<6; i++) xprintf(":%02X"+!i, ifre.ifr_hwaddr.sa_data[i]);
  }
  xputc('\n');

  // If an address is assigned record that.

  ifre.ifr_addr.sa_family = AF_INET;
  memset(&ifre.ifr_addr, 0, sizeof(ifre.ifr_addr));
  ioctl(TT.sockfd, SIOCGIFADDR, &ifre);
  p = (char *)&ifre.ifr_addr;
  for (i = 0; i<sizeof(ifre.ifr_addr); i++) if (p[i]) break;

  if (i != sizeof(ifre.ifr_addr)) {
    struct sockaddr_in *si = (struct sockaddr_in *)&ifre.ifr_addr;
    struct {
      char *name;
      int flag, ioctl;
    } addr[] = {
      {"addr", 0, 0},
      {"P-t-P", IFF_POINTOPOINT, SIOCGIFDSTADDR},
      {"Bcast", IFF_BROADCAST, SIOCGIFBRDADDR},
      {"Mask", 0, SIOCGIFNETMASK}
    };

    xprintf("%10c%s", ' ', (si->sin_family == AF_INET) ? "inet" :
        (si->sin_family == AF_INET6) ? "inet6" : "unspec");

    for (i=0; i < sizeof(addr)/sizeof(*addr); i++) {
      if (!addr[i].flag || (flags & addr[i].flag)) {
        if (addr[i].ioctl && ioctl(TT.sockfd, addr[i].ioctl, &ifre))
          si->sin_family = 0;
        xprintf(" %s:%s ", addr[i].name,
          (si->sin_family == 0xFFFF || !si->sin_family)
            ? "[NOT SET]" : inet_ntoa(si->sin_addr));
      }
    }

    xputc('\n');
  }

  fp = fopen("/proc/net/if_net6", "r");
  if (fp) {
    char iface_name[IFNAMSIZ] = {0,};
    int plen, iscope;

    while(fgets(toybuf, sizeof(toybuf), fp)) {
      int nitems = 0;
      char ipv6_addr[40] = {0,};
      nitems = sscanf(toybuf, "%32s %*08x %02x %02x %*02x %15s\n",
          ipv6_addr+7, &plen, &iscope, iface_name);
      if(nitems != 4) {
        if((nitems < 0) && feof(fp)) break;
        perror_exit("sscanf");
      }
      if(strcmp(name, iface_name) == 0) {
        int i = 0;
        struct sockaddr_in6 sock_in6;
        int len = sizeof(ipv6_addr) / (sizeof ipv6_addr[0]);
        char *ptr = ipv6_addr+7;

        while((i < len-2) && (*ptr)) {
          ipv6_addr[i++] = *ptr++;
          //put ':' after 4th bit
          if(!((i+1) % 5)) ipv6_addr[i++] = ':';
        }
        ipv6_addr[i+1] = '\0';
        if(inet_pton(AF_INET6, ipv6_addr, (struct sockaddr *) &sock_in6.sin6_addr) > 0) {
          sock_in6.sin6_family = AF_INET6;
          if(inet_ntop(AF_INET6, &sock_in6.sin6_addr, toybuf, BUFSIZ)) {
            char *scopes[] = {"Global","Host","Link","Site","Compat"},
                 *scope = "Unknown";
            int j;

            for (j=0; j < sizeof(scopes)/sizeof(*scopes); j++)
              if (iscope == (!!j)<<(j+3)) scope = scopes[j];
            xprintf("%10cinet6 addr: %s/%d Scope: %s\n", ' ', toybuf, plen, scope);
          }
        }
      }
    }
    fclose(fp);
  }

  xprintf("%10c", ' ');

  if (flags) {
    unsigned short mask = 1;
    char **s, *str[] = {
      "UP", "BROADCAST", "DEBUG", "LOOPBACK", "POINTOPOINT", "NOTRAILERS",
      "RUNNING", "NOARP", "PROMISC", "ALLMULTI", "MASTER", "SLAVE", "MULTICAST",
      "PORTSEL", "AUTOMEDIA", "DYNAMIC", NULL
    };

    for(s = str; *s; s++) {
      if(flags & mask) xprintf("%s ", *s);
      mask = mask << 1;
    }
  } else xprintf("[NO FLAGS] ");

  if (ioctl(TT.sockfd, SIOCGIFMTU, &ifre) < 0) ifre.ifr_mtu = 0;
  xprintf(" MTU:%d", ifre.ifr_mtu);
  if (ioctl(TT.sockfd, SIOCGIFMETRIC, &ifre) < 0) ifre.ifr_metric = 0;
  if (!ifre.ifr_metric) ifre.ifr_metric = 1;
  xprintf("  Metric:%d", ifre.ifr_metric);

  // non-virtual interface

  if(val) {
    char *label[] = {"RX bytes", "RX packets", "errors", "dropped", "overruns",
      "frame", 0, 0, "TX bytes", "TX packets", "errors", "dropped", "overruns",
      "collisions", "carrier", 0, "txqueuelen"};
    signed char order[] = {-1, 1, 2, 3, 4, 5, -1, 9, 10, 11, 12, 14, -1,
      13, 16, -1, 0, 8};
    int i;

    // Query txqueuelen
    if (ioctl(TT.sockfd, SIOCGIFTXQLEN, &ifre) >= 0) val[16] = ifre.ifr_qlen;
    else val[16] = -1;

    for (i = 0; i < sizeof(order); i++) {
      int j = order[i];

      if (j < 0) xprintf("\n%10c", ' ');
      else xprintf("%s:%llu ", label[j], val[j]);
    }
  }
  xputc('\n');

  if(!ioctl(TT.sockfd, SIOCGIFMAP, &ifre) && (ifre.ifr_map.irq ||
      ifre.ifr_map.mem_start || ifre.ifr_map.dma || ifre.ifr_map.base_addr))
  {
    xprintf("%10c", ' ');
    if(ifre.ifr_map.irq) xprintf("Interrupt:%d ", ifre.ifr_map.irq);
    if(ifre.ifr_map.base_addr >= 0x100) // IO_MAP_INDEX
      xprintf("Base address:0x%lx ", ifre.ifr_map.base_addr);
    if(ifre.ifr_map.mem_start)
      xprintf("Memory:%lx-%lx ", ifre.ifr_map.mem_start, ifre.ifr_map.mem_end);
    if(ifre.ifr_map.dma) xprintf("DMA chan:%x ", ifre.ifr_map.dma);
    xputc('\n');
  }
  xputc('\n');
}

static void show_iface(char *iface_name)
{
  char *name;
  struct string_list *ifaces = 0, *sl;
  int i, j;
  FILE *fp;

  fp = xfopen("/proc/net/dev", "r");

  for (i=0; fgets(toybuf, sizeof(toybuf), fp); i++) {
    char *buf = toybuf;
    unsigned long long val[17];

    if (i<2) continue;

    while (isspace(*buf)) buf++;
    name = strsep(&buf, ":");
    if(!buf) error_exit("bad name %s", name);

    errno = 0;
    for (j=0; j<16 && !errno; j++) val[j] = strtoll(buf, &buf, 0);
    if (errno) perror_exit("bad %s at %s", name, buf);

    if (iface_name) {
      if (!strcmp(iface_name, name)) {
        display_ifconfig(name, 1, val);

        return;
      }
    } else {
      sl = xmalloc(sizeof(*sl)+strlen(name)+1);
      strcpy(sl->str, name);
      sl->next = ifaces;
      ifaces = sl;

      display_ifconfig(name, toys.optflags & FLAG_a, val);
    }
  }
  fclose(fp);

  if (iface_name) display_ifconfig(iface_name, 1, 0);
  else {
    struct ifconf ifcon;
    struct ifreq *ifre;
    int num;

    // Loop until buffer's big enough
    ifcon.ifc_buf = NULL;
    for (num = 30;;num += 10) {
      ifcon.ifc_len = sizeof(struct ifreq)*num;
      ifcon.ifc_buf = xrealloc(ifcon.ifc_buf, ifcon.ifc_len);
      xioctl(TT.sockfd, SIOCGIFCONF, &ifcon);
      if (ifcon.ifc_len != sizeof(struct ifreq)*num) break;
    }

    ifre = ifcon.ifc_req;
    for(num = 0; num < ifcon.ifc_len && ifre; num += sizeof(struct ifreq), ifre++)
    {
      // Skip duplicates
      for(sl = ifaces; sl; sl = sl->next)
        if(!strcmp(sl->str, ifre->ifr_name)) break;

      if(!sl) display_ifconfig(ifre->ifr_name, toys.optflags & FLAG_a, 0);
    }

    free(ifcon.ifc_buf);
  }

  llist_traverse(ifaces, free);
}

// Encode offset and size of field into an int, and make result negative
#define IFREQ_OFFSZ(x) -(int)((offsetof(struct ifreq, x)<<16) + sizeof(ifre.x))

void ifconfig_main(void)
{
  char **argv = toys.optargs;
  struct ifreq ifre;
  int i;

  if(*argv && (strcmp(*argv, "--help") == 0)) show_help();
  
  TT.sockfd = xsocket(AF_INET, SOCK_DGRAM, 0);
  if(toys.optc < 2) {
    show_iface(*argv);
    return;
  }

  // Open interface
  memset(&ifre, 0, sizeof(struct ifreq));
  xstrncpy(ifre.ifr_name, *argv, IFNAMSIZ);

  // Perform operations on interface
  while(*++argv) {
    struct argh {
      char *name;
      int on, off; // set, clear
    } try[] = {
      {"up", IFF_UP|IFF_RUNNING, 0},
      {"down", 0, IFF_UP},
      {"arp", 0, IFF_NOARP},
      {"trailers", 0, IFF_NOTRAILERS},
      {"promisc", IFF_PROMISC, 0},
      {"allmulti", IFF_ALLMULTI, 0},
      {"multicast", IFF_MULTICAST, 0},
      {"dynamic", IFF_DYNAMIC, 0},
      {"pointopoint", IFF_POINTOPOINT, SIOCSIFDSTADDR},
      {"broadcast", IFF_BROADCAST, SIOCSIFBRDADDR},
      {"netmask", 0, SIOCSIFNETMASK},
      {"dstaddr", 0, SIOCSIFDSTADDR},
      {"mtu", IFREQ_OFFSZ(ifr_mtu), SIOCSIFMTU},
      {"keepalive", IFREQ_OFFSZ(ifr_data), SIOCDEVPRIVATE}, // SIOCSKEEPALIVE
      {"outfill", IFREQ_OFFSZ(ifr_data), SIOCDEVPRIVATE+2}, // SIOCSOUTFILL
      {"metric", IFREQ_OFFSZ(ifr_metric), SIOCSIFMETRIC},
      {"txqueuelen", IFREQ_OFFSZ(ifr_qlen), SIOCSIFTXQLEN},
      {"mem_start", IFREQ_OFFSZ(ifr_map.mem_start), SIOCSIFMAP},
      {"io_addr", IFREQ_OFFSZ(ifr_map.base_addr), SIOCSIFMAP},
      {"irq", IFREQ_OFFSZ(ifr_map.irq), SIOCSIFMAP},
      {"inet", 0, 0},
      {"inet6", 0, 0}
    };
    char *s = *argv;
    int rev = (*s == '-');

    s += rev;

    for (i = 0; i < sizeof(try)/sizeof(*try); i++) {
      struct argh *t = try+i;
      int on = t->on, off = t->off;

      if (strcmp(t->name, s)) continue;

      // Is this an SIOCSI entry?
      if ((off|0xff) == 0x89ff) {
        if (!rev) {
          if (!*++argv) show_help();

          // Assign value to ifre field and call ioctl? (via IFREQ_OFFSZ.)
          if (on < 0) {
            long l = strtoul(*argv, 0, 0);

            if (off == SIOCSIFMAP) xioctl(TT.sockfd, SIOCGIFMAP, &ifre);
            on = -on;
            poke((on>>16) + (char *)&ifre, l, on&15);
            xioctl(TT.sockfd, off, &ifre);
            break;
          } else set_address(*argv, &ifre, off);
        }
        off = 0;
      }

      // Set flags
      if (on || off) {
        xioctl(TT.sockfd, SIOCGIFFLAGS, &ifre);
        ifre.ifr_flags &= ~(rev ? on : off);
        ifre.ifr_flags |= (rev ? off : on);
        xioctl(TT.sockfd, SIOCSIFFLAGS, &ifre);
      }

      break;
    }
    if (i != sizeof(try)/sizeof(*try)) continue;

      if (!strcmp(*argv, "hw")) {
        char *hw_addr, *ptr, *p;
        struct sockaddr *sock = &ifre.ifr_hwaddr;
        int count = 6;

        if (!*++argv) show_help();

        memset(sock, 0, sizeof(struct sockaddr));
        if (!strcmp("ether", *argv)) sock->sa_family = ARPHRD_ETHER;
        else if (!strcmp("infiniband", *argv)) {
          sock->sa_family = ARPHRD_INFINIBAND;
          count = 20;
        } else {
          toys.exithelp++;
          error_exit("bad hw '%s'", *argv);
        }
        hw_addr = *++argv;

        ptr = p = (char *) sock->sa_data;

        while (*hw_addr && (p-ptr) < count) {
          int val, len = 0;

          if (*hw_addr == ':') hw_addr++;
          sscanf(hw_addr, "%2x%n", &val, &len);
          if (len != 2) break;
          hw_addr += len;
          *p++ = val;
        }

        if ((p-ptr) != count || *hw_addr)
          error_exit("bad hw-addr '%s'", hw_addr ? hw_addr : "");
        xioctl(TT.sockfd, SIOCSIFHWADDR, &ifre);

      // Add/remove ipv6 address to interface

      } else if (!strcmp(*argv, "add") || !strcmp(*argv, "del")) {
        sockaddr_with_len *swl = NULL;
        struct ifreq_inet6 ifre6;
        char *prefix;
        int plen = 0, sockfd6 = xsocket(AF_INET6, SOCK_DGRAM, 0);

        if (!argv[1]) show_help();

        prefix = strchr(argv[1], '/');
        if (prefix) {
          plen = get_int_value(prefix + 1, 0, 128);
          *prefix = 0;
        }
        swl = get_sockaddr(argv[1], 0, AF_INET6);
        ifre6.ifrinte6_addr = swl->sock_u.sock_in6.sin6_addr;
        xioctl(sockfd6, SIOCGIFINDEX, &ifre);
        ifre6.ifrinet6_ifindex = ifre.ifr_ifindex;
        ifre6.ifrinet6_prefixlen = plen;
        xioctl(sockfd6, **argv=='a' ? SIOCSIFADDR : SIOCDIFADDR, &ifre6);

        free(swl);
        close(sockfd6);

        argv++;
      } else if (isdigit(**argv) || !strcmp(*argv, "default")) {
          set_address(*argv, &ifre, SIOCSIFADDR);
          //if the interface name is not an alias; set the flag and continue.
          if(!strchr(ifre.ifr_name, ':')) {
            xioctl(TT.sockfd, SIOCGIFFLAGS, &ifre);
            ifre.ifr_flags |= IFF_UP|IFF_RUNNING;
            xioctl(TT.sockfd, SIOCSIFFLAGS, &ifre);
          }

    } else {
      errno = EINVAL;
      toys.exithelp++;
      error_exit("bad argument '%s'", *argv);
    }
  }
  close(TT.sockfd);
}
