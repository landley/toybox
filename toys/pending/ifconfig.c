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
  default n
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
  short flags, hasaddr;
  struct sockaddr addr, dstaddr, broadaddr, netmask, hwaddr;
  struct ifmap map;

  char name[IFNAMSIZ];
  unsigned long long val[16];
};

#define IO_MAP_INDEX 0x100

//from kernel header ipv6.h
#define IPV6_ADDR_ANY 0x0000U
#define IPV6_ADDR_LOOPBACK  0x0010U
#define IPV6_ADDR_LINKLOCAL  0x0020U
#define IPV6_ADDR_SITELOCAL  0x0040U
#define IPV6_ADDR_COMPATv4  0x0080U

//for the param settings.

//for ipv6 add/del
struct ifreq_inet6 {
  struct in6_addr ifrinte6_addr;
  uint32_t ifrinet6_prefixlen;
  int ifrinet6_ifindex;
};

#ifndef SIOCSKEEPALIVE
# define SIOCSKEEPALIVE  (SIOCDEVPRIVATE)        /* Set keepalive timeout in sec */
# define SIOCGKEEPALIVE  (SIOCDEVPRIVATE+1)        /* Get keepalive timeout */
#endif

#ifndef SIOCSOUTFILL
# define SIOCSOUTFILL  (SIOCDEVPRIVATE+2)        /* Set outfill timeout */
# define SIOCGOUTFILL  (SIOCDEVPRIVATE+3)        /* Get outfill timeout */
#endif

#ifndef INFINIBAND_ALEN
# define INFINIBAND_ALEN 20
#endif

/*
 * verify the host is local unix path.
 * if so, set the swl input param accordingly.
 */
static int is_host_unix(char *host, sockaddr_with_len **swl)
{
  if (strncmp(host, "local:", 6) == 0) {
    struct sockaddr_un *sockun;

    *swl = xzalloc(sizeof(struct sockaddr_with_len));
    (*swl)->socklen = sizeof(struct sockaddr_un);
    (*swl)->sock_u.sock.sa_family = AF_UNIX;
    sockun = (struct sockaddr_un *)&(*swl)->sock_u.sock;
    xstrncpy(sockun->sun_path, host + 6, sizeof(sockun->sun_path));
    return 1;
  }
  return 0;
}

/*
 * used to converts string into int and validate the input str for invalid int value or out-of-range.
 */
unsigned get_strtou(char *str, char **endp, int base)
{
  unsigned long uli;
  char *endptr;

  if(!isalnum(str[0])) {
    errno = ERANGE;
    return UINT_MAX;
  }
  errno = 0;
  uli = strtoul(str, &endptr, base);
  if(uli > UINT_MAX) {
    errno = ERANGE;
    return UINT_MAX;
  }

  if(endp) *endp = endptr;
  if(endptr[0]) {
    if(isalnum(endptr[0]) || errno) { //"123abc" or out-of-range
      errno = ERANGE;
      return UINT_MAX;
    }
    errno = EINVAL;
  }
  return uli;
}



/*
 * validate the input param (host) for valid ipv6 ip and extract port number (if there).
 */
static void get_host_and_port(char **host, int *port)
{
  char *ch_ptr;
  char *org_host = *host;
  if (*host[0] == '[') {
    (*host)++;
    ch_ptr = strchr(*host, ']');
    if (!ch_ptr || (ch_ptr[1] != ':' && ch_ptr[1] != '\0'))
      error_exit("bad address '%s'", org_host);
  } else {
    ch_ptr = strrchr(*host, ':');
    //There is more than one ':' like "::1"
    if(ch_ptr && strchr(*host, ':') != ch_ptr) ch_ptr = NULL;
  }
  if (ch_ptr) { //pointer to ":" or "]:"
    int size = ch_ptr - (*host) + 1;
    xstrncpy(*host, *host, size);
    if (*ch_ptr != ':') {
      ch_ptr++; //skip ']'
      //[nn] without port
      if (!*ch_ptr) return;
    }
    ch_ptr++; //skip ':' to get the port number.
    *port = get_strtou(ch_ptr, NULL, 10);
    if (errno || (unsigned)*port > 65535) error_exit("bad port '%s'", org_host);
  }
}

/*
 * used to extract the address info from the given host ip
 * and update the swl param accordingly.
 */
static int get_socket_stream(char *host, sa_family_t af, sockaddr_with_len **swl)
{
  struct addrinfo hints, *result, *rp;
  int status;

  memset(&hints, 0 , sizeof(struct addrinfo));
  hints.ai_family = af;
  hints.ai_socktype = SOCK_STREAM;

  status = getaddrinfo(host, NULL, &hints, &result);
  if (status) error_exit("bad address '%s' : %s", host, gai_strerror(status));

  for (rp = result; rp; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET || rp->ai_family == AF_INET6) {
      *swl = xmalloc(sizeof(struct sockaddr_with_len));
      (*swl)->socklen = rp->ai_addrlen;
      memcpy(&((*swl)->sock_u.sock), rp->ai_addr, rp->ai_addrlen);
      break;
    }
  }
  freeaddrinfo(result);
  return rp ? 0 : -1;
}

/*
 * use to get the socket address with the given host ip.
 */
sockaddr_with_len *get_sockaddr(char *host, int port, sa_family_t af)
{
  sockaddr_with_len *swl = NULL;
  in_port_t port_num = htons(port);

  if(is_host_unix(host, &swl) && swl) return swl;

  //[IPV6_ip]:port_num
  if(host[0] == '[' || strrchr(host, ':')) get_host_and_port((char **)&host, &port);

  if (get_socket_stream(host, af, &swl)) return NULL;

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

static void set_flags(int sockfd, struct ifreq *ifre, int set_flag, int reset_flag)
{
  xioctl(sockfd, SIOCGIFFLAGS, ifre);
  ifre->ifr_flags = (ifre->ifr_flags & (~reset_flag)) | set_flag;
  xioctl(sockfd, SIOCSIFFLAGS, ifre);
}

static void set_mtu(int sockfd, struct ifreq *ifre, char *mtu)
{
  ifre->ifr_mtu = strtoul(mtu, NULL, 0);
  xioctl(sockfd, SIOCSIFMTU, ifre);
}

static void set_metric(int sockfd, struct ifreq *ifre, char *metric)
{
  ifre->ifr_metric = strtoul(metric, NULL, 0);
  xioctl(sockfd, SIOCSIFMETRIC, ifre);
}

static void set_qlen(int sockfd, struct ifreq *ifre, char *qlen)
{
  ifre->ifr_qlen = strtoul(qlen, NULL, 0);
  xioctl(sockfd, SIOCSIFTXQLEN, ifre);
}

static void set_ipv6_addr(int sockfd, struct ifreq *ifre, char *ipv6_addr, int request, char *req_name)
{
  char *prefix;
  int plen = 0;
  sockaddr_with_len *swl = NULL;

  prefix = strchr(ipv6_addr, '/');
  if(prefix) {
    plen = get_int_value(prefix + 1, 0, 128);
    *prefix = '\0';
  }
  swl = get_sockaddr(ipv6_addr, 0, AF_INET6);
  if(!swl) error_exit("error in resolving host name");
    int sockfd6;
    struct ifreq_inet6 ifre6;
    memcpy((char *) &ifre6.ifrinte6_addr,
        (char *) &(swl->sock_u.sock_in6.sin6_addr),
        sizeof(struct in6_addr));
    //Create a channel to the NET kernel.
    sockfd6 = xsocket(AF_INET6, SOCK_DGRAM, 0);
    xioctl(sockfd6, SIOGIFINDEX, ifre);
    ifre6.ifrinet6_ifindex = ifre->ifr_ifindex;
    ifre6.ifrinet6_prefixlen = plen;

    xioctl(sockfd6, request, &ifre6);
    free(swl);
}

static void set_address(int sockfd, char *host_name, struct ifreq *ifre, int request, char *req_name)
{
  struct sockaddr_in sock_in;
  sockaddr_with_len *swl = NULL;
  sock_in.sin_family = AF_INET;
  sock_in.sin_port = 0;

  //Default 0.0.0.0
  if(strcmp(host_name, "default") == 0) sock_in.sin_addr.s_addr = INADDR_ANY;
  else {
    swl = get_sockaddr(host_name, 0, AF_INET);
    if(!swl) error_exit("error in resolving host name");

    sock_in.sin_addr = swl->sock_u.sock_in.sin_addr;
  }
  memcpy((char *)&ifre->ifr_addr, (char *) &sock_in, sizeof(struct sockaddr));
  xioctl(sockfd, request, ifre);

  if(swl != NULL) {
    free(swl);
    swl = NULL;
  }
}

static int hex_to_binary(char *hw_addr, struct sockaddr *sock, int count)
{
  int i = 0, j = 0;
  unsigned char nib_val, ch;

  char *ptr = (char *) sock->sa_data;
  if (count == ETH_ALEN) sock->sa_family = ARPHRD_ETHER;
  else if (count == INFINIBAND_ALEN) sock->sa_family = ARPHRD_INFINIBAND;
  else return -1;
  //e.g. hw_addr "62:2D:A6:9E:2D:BE"
  for (; *hw_addr && (i < count); i++) {
    if (*hw_addr == ':') hw_addr++;
    j = nib_val = 0;
    for (;j < 2; j++) {
      ch = *hw_addr;
      //0-9 = 10 chars.
      if (((unsigned char)(ch - '0')) < 10) ch = (ch - '0');
      //a-f = 6 chars.
      else if (((unsigned char)((ch) - 'a')) < 6) ch = (ch - ('a'-10));
      //A-F = 6 chars.
      else if (((unsigned char)((ch) - 'A')) < 6) ch = (ch - ('A'-10));
      else if (j && (ch == ':' || ch == 0)) break;
      else return -1;
      hw_addr++;
      nib_val <<= 4;
      nib_val += ch;
    }
    *ptr++ = nib_val;
  }
  if (*hw_addr) return -1;
  return 0;
}

static void set_hw_address(int sockfd, char ***argv, struct ifreq *ifre, int request, char *req_name)
{
  int hw_class = 0;
  char *hw_addr;
  struct sockaddr sock;
  char *ptr;
  char *hw_class_strings[] = {
      "ether",
      "infiniband",
      NULL
  };

  if(strcmp(hw_class_strings[0], **argv) == 0)
    hw_class = 1;
  else if(strcmp(hw_class_strings[1], **argv) == 0)
    hw_class = 2;
  if(!hw_class || !(*argv += 1)) {
    errno = EINVAL;
    toys.exithelp++;
    error_exit("bad hardware class");
  }

  memset(&sock, 0, sizeof(struct sockaddr));
  hw_addr = **argv;
  if(hex_to_binary(hw_addr, &sock, hw_class == 1 ? ETH_ALEN : INFINIBAND_ALEN))
    error_exit("bad hw-addr %s", hw_addr);
  ptr = (char *)&sock;
  memcpy( ((char *) ifre) + offsetof(struct ifreq, ifr_hwaddr), ptr, sizeof(struct sockaddr));
  xioctl(sockfd, request, ifre);
}

static void set_memstart(int sockfd, struct ifreq *ifre, char *start_addr, int request, char *req_name)
{
  unsigned long mem_start = strtoul(start_addr, NULL, 0);

  xioctl(sockfd, SIOCGIFMAP, ifre);
  ifre->ifr_map.mem_start = mem_start;
  xioctl(sockfd, request, ifre);
}

static void set_ioaddr(int sockfd, struct ifreq *ifre, char *baddr, int request, char *req_name)
{
  unsigned short int base_addr = strtoul(baddr, NULL, 0);
  xioctl(sockfd, SIOCGIFMAP, ifre);
  ifre->ifr_map.base_addr = base_addr;
  xioctl(sockfd, request, ifre);
}

static void set_irq(int sockfd, struct ifreq *ifre, char *irq_val, int request, char *req_name)
{
  unsigned short int irq = strtoul(irq_val, NULL, 0);
  char *ptr;
  struct ifmap *map;

  xioctl(sockfd, SIOCGIFMAP, ifre);

  ptr = ((char *) ifre) + offsetof(struct ifreq, ifr_map);
  map = (struct ifmap *)ptr;
  map->irq = irq;
  xioctl(sockfd, request, ifre);
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
  int sokfd;

  sokfd = xsocket(AF_INET, SOCK_DGRAM, 0);
  xstrncpy(ifre.ifr_name, name, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFFLAGS, &ifre)<0) perror_exit("%s", il->name);
  il->flags = ifre.ifr_flags;

  if(ioctl(sokfd, SIOCGIFHWADDR, &ifre) >= 0)
    memcpy(il->hwaddr.sa_data, ifre.ifr_hwaddr.sa_data, sizeof(il->hwaddr.sa_data));

  il->hw_type = ifre.ifr_hwaddr.sa_family;

  if(ioctl(sokfd, SIOCGIFMETRIC, &ifre) >= 0)
    il->metric = ifre.ifr_metric;

  if(ioctl(sokfd, SIOCGIFMTU, &ifre) >= 0)
    il->mtu = ifre.ifr_mtu;

  if(ioctl(sokfd, SIOCGIFMAP, &ifre) == 0)
    il->map = ifre.ifr_map;

  il->txqueuelen = -1;
  if(ioctl(sokfd, SIOCGIFTXQLEN, &ifre) >= 0)
    il->txqueuelen = ifre.ifr_qlen;

  ifre.ifr_addr.sa_family = AF_INET;

  if(!ioctl(sokfd, SIOCGIFADDR, &ifre)) {
    il->hasaddr = 1;
    il->addr = ifre.ifr_addr;
    if(ioctl(sokfd, SIOCGIFDSTADDR, &ifre) >= 0)
      il->dstaddr = ifre.ifr_dstaddr;

    if(ioctl(sokfd, SIOCGIFBRDADDR, &ifre) >= 0)
      il->broadaddr = ifre.ifr_broadaddr;

    if(ioctl(sokfd, SIOCGIFNETMASK, &ifre) >= 0)
      il->netmask = ifre.ifr_netmask;
  }
  close(sokfd);
}

static void show_ip_addr(char *name, struct sockaddr *skaddr)
{
  char *s = "[NOT SET]";

  if(skaddr->sa_family != 0xFFFF && skaddr->sa_family)
    s = inet_ntoa(((struct sockaddr_in *)skaddr)->sin_addr);

  xprintf(" %s:%s ", name, s);
}

static void print_ip6_addr(struct if_list *il)
{
  char iface_name[IFNAMSIZ] = {0,};
  int plen, scope;
  FILE *fp;

  if(!(fp = fopen("/proc/net/if_net6", "r"))) return;

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
          xprintf("%10sinet6 addr: %s/%d Scope:", " ", toybuf, plen);
          if(scope == IPV6_ADDR_ANY) xprintf(" Global");
          else if(scope == IPV6_ADDR_LOOPBACK) xprintf(" Host");
          else if(scope == IPV6_ADDR_LINKLOCAL) xprintf(" Link");
          else if(scope == IPV6_ADDR_SITELOCAL) xprintf(" Site");
          else if(scope == IPV6_ADDR_COMPATv4) xprintf(" Compat");
          else xprintf("Unknown");
          xputc('\n');
        }
      }
    }
  }
  fclose(fp);
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

  for (i=0; i < (sizeof(types)/sizeof(*types))-1; i++)
    if (il->hw_type == types[i].type) break;

  xprintf("%-9s Link encap:%s  ", il->name, types[i].title);
  if(il->hwaddr.sa_data && il->hw_type == ARPHRD_ETHER) {
    xprintf("HWaddr ");
    for (i=0; i<6; i++) xprintf(":%02X"+!i, il->hwaddr.sa_data[i]);
  }
  xputc('\n');

  if(il->hasaddr) {
    int af = il->addr.sa_family;
    char *name = "unspec";

    if (af == AF_INET) name = "inet";
    else if (af == AF_INET6) name = "inet6";
    xprintf("%10c%s", ' ', name);

    show_ip_addr("addr", &il->addr);
    if(il->flags & IFF_POINTOPOINT) show_ip_addr("P-t-P", &il->dstaddr);
    if(il->flags & IFF_BROADCAST) show_ip_addr("Bcast", &il->broadaddr);
    show_ip_addr("Mask", &il->netmask);
    xputc('\n');
  }

  print_ip6_addr(il);
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
  int num_of_req = 30;
  struct ifconf ifcon;
  struct ifreq *ifre;
  int num, sokfd;

  ifcon.ifc_buf = NULL;
  sokfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sokfd < 0) perror_exit("socket");
  for (;;) {
    ifcon.ifc_len = sizeof(struct ifreq) * num_of_req; //Size of buffer.
    ifcon.ifc_buf = xrealloc(ifcon.ifc_buf, ifcon.ifc_len);

    xioctl(sokfd, SIOCGIFCONF, &ifcon);
    //in case of overflow, increase number of requests and retry.
    if (ifcon.ifc_len == (int)(sizeof(struct ifreq) * num_of_req)) {
      num_of_req += 10;
      continue;
    }
    break;
  }

  ifre = ifcon.ifc_req;
  for(num = 0; num < ifcon.ifc_len && ifre; num += sizeof(struct ifreq), ifre++) {
    //Escape duplicate values from the list.
    struct if_list *il;

    for(il = TT.if_list; il; il = il->next)
      if(!strcmp(ifre->ifr_name, il->name)) break;
    if(!il) {
      il = xzalloc(sizeof(struct if_list));
      xstrncpy(il->name, ifre->ifr_name, IFNAMSIZ);
      add_iface_to_list(il);
      errno = 0;
      get_device_info(il);
    }
  }

  close(sokfd);
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
    errno = 0;
    get_device_info(il);
  }
  fclose(fp);

  if(iface_name) {
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
      errno = 0;
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

void ifconfig_main(void)
{
  char **argv = toys.optargs;

  if(*argv && (strcmp(*argv, "--help") == 0)) show_help();
  
  if(toys.optc < 2) {
    show_iface(*argv);
    return;
  }

  //set ifconfig params.
  {
    struct ifreq ifre;
    int sockfd = 0;
    //get interface name
    memset(&ifre, 0, sizeof(struct ifreq));
    strncpy(ifre.ifr_name, *argv, IFNAMSIZ);
    ifre.ifr_name[IFNAMSIZ-1] = 0;
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) perror_exit("socket");

    while(*++argv != NULL) {
      /* flags settings */
      if (!strcmp(*argv, "up"))
        set_flags(sockfd, &ifre, IFF_UP | IFF_RUNNING, 0);
      else if (!strcmp(*argv, "down"))
        set_flags(sockfd, &ifre, 0, IFF_UP);

      else if (!strcmp(*argv, "arp"))
        set_flags(sockfd, &ifre, 0, IFF_NOARP);
      else if (!strcmp(*argv, "-arp"))
        set_flags(sockfd, &ifre, IFF_NOARP, 0);
      else if (!strcmp(*argv, "trailers"))
        set_flags(sockfd, &ifre, 0, IFF_NOTRAILERS);
      else if (!strcmp(*argv, "-trailers"))
        set_flags(sockfd, &ifre, IFF_NOTRAILERS, 0);

      else if (!strcmp(*argv, "promisc"))
        set_flags(sockfd, &ifre, IFF_PROMISC, 0);
      else if (!strcmp(*argv, "-promisc"))
        set_flags(sockfd, &ifre, 0, IFF_PROMISC);
      else if (!strcmp(*argv, "allmulti"))
        set_flags(sockfd, &ifre, IFF_ALLMULTI, 0);
      else if (!strcmp(*argv, "-allmulti"))
        set_flags(sockfd, &ifre, 0, IFF_ALLMULTI);
      else if (!strcmp(*argv, "multicast"))
        set_flags(sockfd, &ifre, IFF_MULTICAST, 0);
      else if (!strcmp(*argv, "-multicast"))
        set_flags(sockfd, &ifre, 0, IFF_MULTICAST);
      else if (!strcmp(*argv, "dynamic"))
        set_flags(sockfd, &ifre, IFF_DYNAMIC, 0);
      else if (!strcmp(*argv, "-dynamic"))
        set_flags(sockfd, &ifre, 0, IFF_DYNAMIC);
      else if (!strcmp(*argv, "-pointopoint"))
        set_flags(sockfd, &ifre, 0, IFF_POINTOPOINT);
      /*value setup */
      else if (!strcmp(*argv, "pointopoint")) {
        if (!*++argv) show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFDSTADDR, "SIOCSIFDSTADDR");
        set_flags(sockfd, &ifre, IFF_POINTOPOINT, 0);
      } else if (!strcmp(*argv, "netmask")) {
        if (!*++argv) show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFNETMASK, "SIOCSIFNETMASK");
      } else if (!strcmp(*argv, "-broadcast")) {
        set_flags(sockfd, &ifre, 0, IFF_BROADCAST);
      } else if (!strcmp(*argv, "broadcast")) {
        if (!*++argv) show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFBRDADDR, "SIOCSIFBRDADDR");
        set_flags(sockfd, &ifre, IFF_BROADCAST, 0);
      } else if (!strcmp(*argv, "dstaddr")) {
        if (!*++argv) show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFDSTADDR, "SIOCSIFDSTADDR");
      } else if (!strcmp(*argv, "hw")) {
        if (!*++argv) show_help();
        set_hw_address(sockfd, &argv, &ifre, SIOCSIFHWADDR, "SIOCSIFHWADDR");
      } else if (!strcmp(*argv, "mtu")) {
        if (!*++argv) show_help();
        set_mtu(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "metric")) {
        if (!*++argv) show_help();
        set_metric(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "txqueuelen")) {
        if (!*++argv) show_help();
        set_qlen(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "keepalive")) {
        if (!*++argv) show_help();
        ifre.ifr_data = (void *)strtoul(*argv, 0, 0);
        xioctl(sockfd, SIOCSKEEPALIVE, &ifre);
      } else if (!strcmp(*argv, "outfill")) {
        if (!*++argv) show_help();
        ifre.ifr_data = (void *)strtoul(*argv, 0, 0);
        xioctl(sockfd, SIOCSOUTFILL, &ifre);
      } else if (!strcmp(*argv, "add")) {
        if (!*++argv) show_help();
        set_ipv6_addr(sockfd, &ifre, *argv, SIOCSIFADDR, "SIOCSIFADDR");
      } else if (!strcmp(*argv, "del")) {
        if (!*++argv) show_help();
        set_ipv6_addr(sockfd, &ifre, *argv, SIOCDIFADDR, "SIOCDIFADDR");
      } else if (!strcmp(*argv, "mem_start")) {
        if (!*++argv) show_help();
        set_memstart(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else if (!strcmp(*argv, "io_addr")) {
        if (!*++argv) show_help();
        set_ioaddr(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else if (!strcmp(*argv, "irq")) {
        if (!*++argv) show_help();
        set_irq(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else {
        if (isdigit(**argv) || !strcmp(*argv, "default")) {
          char *iface_name = ifre.ifr_name;
          short int is_colon = 0;
          set_address(sockfd, *argv, &ifre, SIOCSIFADDR, "SIOCSIFADDR");
          while (*iface_name) {
            if (*iface_name == ':') {
              is_colon = 1;
              break;
            }
            iface_name++;
          }
          //if the interface name is not an alias; set the flag and continue.
          if(!is_colon) set_flags(sockfd, &ifre, IFF_UP | IFF_RUNNING, 0);
        } else if (!strcmp(*argv, "inet") || !strcmp(*argv, "inet6")) continue;
        else {
          errno = EINVAL;
          toys.exithelp++;
          error_exit("bad argument");
        }
      }

    }
    if(sockfd > 0) close(sockfd);
  }
}
