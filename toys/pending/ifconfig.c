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
#include "toynet.h"

#include <net/if_arp.h>
#include <net/ethernet.h>

GLOBALS(
  void *if_list;

  int sockfd;
)

typedef struct sockaddr_with_len {
  union {
    struct sockaddr sock;
    struct sockaddr_in sock_in;
    struct sockaddr_in6 sock_in6;
  }sock_u;
  socklen_t socklen;
} sockaddr_with_len;

// man netdevice
struct if_list {
  struct if_list *next;
  int hw_type, mtu, metric, txqueuelen, non_virtual_iface;
  short flags;
  struct sockaddr addr, dstaddr, broadaddr, netmask, hwaddr;
  struct ifmap map;

  char name[IFNAMSIZ];
  unsigned long long val[16];
};

#define IO_MAP_INDEX 0x100

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
    swl->socklen = sizeof(struct sockaddr_un);
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
      swl->socklen = rp->ai_addrlen;
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

/*
 * get the numeric hostname and service name, for a given socket address.
 */
char *address_to_name(struct sockaddr *sock)
{
  //man page of getnameinfo.
  char hbuf[NI_MAXHOST] = {0,}, sbuf[NI_MAXSERV] = {0,};
  int status = 0;

  if(sock->sa_family == AF_INET) {
    socklen_t len = sizeof(struct sockaddr_in);
    if((status = getnameinfo(sock, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) == 0)
      return xmsprintf("%s:%s", hbuf, sbuf);
    else {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(status));
      return NULL;
    }
  } else if(sock->sa_family == AF_INET6) {
    socklen_t len = sizeof(struct sockaddr_in6);
    if((status = getnameinfo(sock, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) == 0) {
      //verification for resolved hostname.
      if(strchr(hbuf, ':')) return xmsprintf("[%s]:%s", hbuf, sbuf);
      else return xmsprintf("%s:%s", hbuf, sbuf);
    } else {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(status));
      return NULL;
    }
  } else if(sock->sa_family == AF_UNIX) {
    struct sockaddr_un *sockun = (void*)sock;
    return xmsprintf("local:%.*s", (int) sizeof(sockun->sun_path), sockun->sun_path);
  } else return NULL;
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

static void add_iface_to_list(struct if_list *newnode)
{
  struct if_list *head_ref = TT.if_list;

  if(!head_ref || strcmp(newnode->name, head_ref->name) < 0) {
    newnode->next = head_ref;
    head_ref = newnode;
  } else {
    struct if_list *current = head_ref;
    while(current->next && strcmp(current->next->name, newnode->name) < 0)
      current = current->next;
    newnode->next = current->next;
    current->next = newnode;
  }
  TT.if_list = (void *)head_ref;
}

static void get_device_info(struct if_list *il)
{
  struct ifreq ifre;
  char *name = il->name;

  il->txqueuelen = -1;

  xstrncpy(ifre.ifr_name, name, IFNAMSIZ);
  if (ioctl(TT.sockfd, SIOCGIFFLAGS, &ifre)<0) perror_exit("%s", il->name);
  il->flags = ifre.ifr_flags;

  if (ioctl(TT.sockfd, SIOCGIFHWADDR, &ifre) >= 0)
    memcpy(il->hwaddr.sa_data, ifre.ifr_hwaddr.sa_data, sizeof(il->hwaddr.sa_data));
  il->hw_type = ifre.ifr_hwaddr.sa_family;

  if (ioctl(TT.sockfd, SIOCGIFMETRIC, &ifre) >= 0)
    il->metric = ifre.ifr_metric;
  if (ioctl(TT.sockfd, SIOCGIFMTU, &ifre) >= 0) il->mtu = ifre.ifr_mtu;
  if (ioctl(TT.sockfd, SIOCGIFMAP, &ifre) == 0) il->map = ifre.ifr_map;
  if (ioctl(TT.sockfd, SIOCGIFTXQLEN, &ifre) >= 0)
    il->txqueuelen = ifre.ifr_qlen;

  // If an address is assigned record that.

  ifre.ifr_addr.sa_family = AF_INET;
  if (!ioctl(TT.sockfd, SIOCGIFADDR, &ifre)) il->addr = ifre.ifr_addr;
  if (ioctl(TT.sockfd, SIOCGIFDSTADDR, &ifre) >= 0)
    il->dstaddr = ifre.ifr_dstaddr;
  if (ioctl(TT.sockfd, SIOCGIFBRDADDR, &ifre) >= 0)
    il->broadaddr = ifre.ifr_broadaddr;
  if (ioctl(TT.sockfd, SIOCGIFNETMASK, &ifre) >= 0)
    il->netmask = ifre.ifr_netmask;
}

static void display_ifconfig(struct if_list *il)
{
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

  for (i=0; i < (sizeof(types)/sizeof(*types))-1; i++)
    if (il->hw_type == types[i].type) break;

  xprintf("%-9s Link encap:%s  ", il->name, types[i].title);
  if(il->hwaddr.sa_data && il->hw_type == ARPHRD_ETHER) {
    xprintf("HWaddr ");
    for (i=0; i<6; i++) xprintf(":%02X"+!i, il->hwaddr.sa_data[i]);
  }
  xputc('\n');

  p = (char *)&il->addr;
  for (i = 0; i<sizeof(il->addr); i++) if (p[i]) break;
  if (i != sizeof(il->addr)) {
    int af = il->addr.sa_family;
    struct {
      char *name;
      int flag, offset;
    } addr[] = {
      {"addr", 0, offsetof(struct if_list, addr)},
      {"P-t-P", IFF_POINTOPOINT, offsetof(struct if_list, dstaddr)},
      {"Bcast", IFF_BROADCAST, offsetof(struct if_list, broadaddr)},
      {"Mask", 0, offsetof(struct if_list, netmask)}
    };

    xprintf("%10c%s", ' ',
      (af == AF_INET) ? "inet" : (af == AF_INET6) ? "inet6" : "unspec");

    for (i=0; i < sizeof(addr)/sizeof(*addr); i++) {
      struct sockaddr_in *s = (struct sockaddr_in *)(addr[i].offset+(char *)il);

      if (!addr[i].flag || (il->flags & addr[i].flag))
        xprintf(" %s:%s ", addr[i].name,
          (s->sin_family == 0xFFFF || !s->sin_family) ? "[NOT SET]" :
            inet_ntoa(s->sin_addr));
    }

    xputc('\n');
  }

  fp = fopen("/proc/net/if_net6", "r");
  if (fp) {
    char iface_name[IFNAMSIZ] = {0,};
    int plen, scope;

    while(fgets(toybuf, sizeof(toybuf), fp)) {
      int nitems = 0;
      char ipv6_addr[40] = {0,};
      nitems = sscanf(toybuf, "%32s %*08x %02x %02x %*02x %15s\n",
          ipv6_addr+7, &plen, &scope, iface_name);
      if(nitems != 4) {
        if((nitems < 0) && feof(fp)) break;
        perror_exit("sscanf");
      }
      if(strcmp(il->name, iface_name) == 0) {
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
            char *names[] = {"Global","Host","Link","Site","Compat"},
                 *name = "Unknown";
            int j;

            for (j=0; j < sizeof(names)/sizeof(*names); j++)
              if (scope == (!!j)<<(j+3)) name = names[j];
            xprintf("%10cinet6 addr: %s/%d Scope: %s\n", ' ', toybuf, plen, name);
          }
        }
      }
    }
    fclose(fp);
  }

  xprintf("%10c", ' ');

  if (il->flags) {
    unsigned short mask = 1;
    char **s, *str[] = {
      "UP", "BROADCAST", "DEBUG", "LOOPBACK", "POINTOPOINT", "NOTRAILERS",
      "RUNNING", "NOARP", "PROMISC", "ALLMULTI", "MASTER", "SLAVE", "MULTICAST",
      "PORTSEL", "AUTOMEDIA", "DYNAMIC", NULL
    };

    for(s = str; *s; s++) {
      if(il->flags & mask) xprintf("%s ", *s);
      mask = mask << 1;
    }
  } else xprintf("[NO FLAGS] ");

  if(!il->metric) il->metric = 1;
  xprintf(" MTU:%d  Metric:%d", il->mtu, il->metric);

  if(il->non_virtual_iface) {
    char *label[] = {"RX bytes", "RX packets", "errors", "dropped", "overruns",
      "frame", 0, 0, "TX bytes", "TX packets", "errors", "dropped", "overruns",
      "collisions", "carrier", 0, "txqueuelen"};
    signed char order[] = {-1, 1, 2, 3, 4, 5, -1, 9, 10, 11, 12, 14, -1,
      13, 16, -1, 0, 8};
    int i;

    for (i = 0; i < sizeof(order); i++) {
      int j = order[i];

      if (j < 0) xprintf("\n%10c", ' ');
      else xprintf("%s:%llu ", label[j],
        j==16 ? (unsigned long long)il->txqueuelen : il->val[j]);
    }
  }
  xputc('\n');

  if(il->map.irq || il->map.mem_start || il->map.dma || il->map.base_addr) {
    xprintf("%10c", ' ');
    if(il->map.irq) xprintf("Interrupt:%d ", il->map.irq);
    if(il->map.base_addr >= IO_MAP_INDEX)
      xprintf("Base address:0x%lx ", il->map.base_addr);
    if(il->map.mem_start)
      xprintf("Memory:%lx-%lx ", il->map.mem_start, il->map.mem_end);
    if(il->map.dma) xprintf("DMA chan:%x ", il->map.dma);
    xputc('\n');
  }
  xputc('\n');
}

static void readconf(void)
{
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
    struct if_list *il;

    // Skip duplicates
    for(il = TT.if_list; il; il = il->next)
      if(!strcmp(ifre->ifr_name, il->name)) break;
    if(!il) {
      il = xzalloc(sizeof(struct if_list));
      xstrncpy(il->name, ifre->ifr_name, IFNAMSIZ);
      add_iface_to_list(il);
      get_device_info(il);
    }
  }

  free(ifcon.ifc_buf);
}

static void show_iface(char *iface_name)
{
  struct if_list *il;
  int i, j;
  FILE *fp;

  fp = xfopen("/proc/net/dev", "r");

  for (i=0; fgets(toybuf, sizeof(toybuf), fp); i++) {
    char *name, *buf;

    if (i<2) continue;

    il = xzalloc(sizeof(struct if_list));
    for (buf = toybuf; isspace(*buf); buf++);
    name = strsep(&buf, ":");
    if(!buf) error_exit("bad name %s", name);
    xstrncpy(il->name, name, IFNAMSIZ);

    errno = 0;
    for (j=0; j<16 && !errno; j++) il->val[j] = strtoll(buf, &buf, 0);
    if (errno) perror_exit("bad %s at %s", name, buf);

    add_iface_to_list(il);
    il->non_virtual_iface = 1;
    get_device_info(il);
  }
  fclose(fp);

  if (iface_name) {
    for(il = TT.if_list; il; il = il->next) {
      if(!strcmp(il->name, iface_name)) {
        display_ifconfig(il);
        break;
      }
    }
    //if the given interface is not in the list.
    if(!il) {
      il = xzalloc(sizeof(struct if_list));
      xstrncpy(il->name, iface_name, IFNAMSIZ);
      get_device_info(il);
      display_ifconfig(il);
      free(il);
    }
  } else {
    readconf();
    for(il = TT.if_list; il; il = il->next)
      if((il->flags & IFF_UP) || (toys.optflags & FLAG_a))
        display_ifconfig(il);
  }

  if (CFG_TOYBOX_FREE) llist_traverse(TT.if_list, free);
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
