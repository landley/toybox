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

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>

GLOBALS(
  void *iface_list;
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
struct iface_list {
  struct iface_list *next;
  int hw_type, ifrmtu, ifrmetric, txqueuelen, non_virtual_iface;
  short ifrflags, ifaddr;
  struct sockaddr ifraddr, ifrdstaddr, ifrbroadaddr, ifrnetmask, ifrhwaddr;
  struct ifmap ifrmap;

  char        ifrname[IFNAMSIZ]; //interface name.
  unsigned long long   receive_bytes; //total bytes received
  unsigned long long   receive_packets; //total packets received
  unsigned long     receive_errors; //bad packets received
  unsigned long     receive_drop; //no space in linux buffers
  unsigned long     receive_fifo; //receiver fifo overrun
  unsigned long     receive_frame; //received frame alignment error
  unsigned long     receive_compressed;
  unsigned long     receive_multicast; //multicast packets received

  unsigned long long   transmit_bytes; //total bytes transmitted
  unsigned long long   transmit_packets; //total packets transmitted
  unsigned long     transmit_errors; //packet transmit problems
  unsigned long     transmit_drop; //no space available in linux
  unsigned long     transmit_fifo;
  unsigned long     transmit_colls;
  unsigned long     transmit_carrier;
  unsigned long     transmit_compressed; //num_tr_compressed;
};

#define HW_NAME_LEN 20
#define HW_TITLE_LEN 30

typedef struct _hw_info {
  char hw_name[HW_NAME_LEN];
  char hw_title[HW_TITLE_LEN];
  int     hw_addrlen;
} HW_INFO;

#define NO_RANGE -1
#define IO_MAP_INDEX 0x100

static int show_iface(char *iface_name);
static void print_ip6_addr(struct iface_list *il);

//from /usr/include/linux/netdevice.h
#ifdef IFF_PORTSEL
//Media selection options.
# ifndef IF_PORT_UNKNOWN
enum {
    IF_PORT_UNKNOWN = 0,
    IF_PORT_10BASE2,
    IF_PORT_10BASET,
    IF_PORT_AUI,
    IF_PORT_100BASET,
    IF_PORT_100BASETX,
    IF_PORT_100BASEFX
};
# endif
#endif

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

void xioctl(int fd, int request, void *data)
{
  if (ioctl(fd, request, data) < 0) perror_exit("ioctl %d", request);
}

char *safe_strncpy(char *dst, char *src, size_t size)
{
  if(!size) return dst;
  dst[--size] = '\0';
  return strncpy(dst, src, size);
}

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
    safe_strncpy(sockun->sun_path, host + 6, sizeof(sockun->sun_path));
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
    safe_strncpy(*host, *host, size);
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
    if( (sockfd6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
      perror_exit("AF_INET6 SOCK_DGRAM", 0);
    xioctl(sockfd6, SIOGIFINDEX, ifre);
    ifre6.ifrinet6_ifindex = ifre->ifr_ifindex;
    ifre6.ifrinet6_prefixlen = plen;

    xioctl(sockfd6, request, &ifre6);
    if(swl != NULL) {
      free(swl);
      swl = NULL;
    }
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

/* Display ifconfig info. */
static void get_proc_info(char *buff, struct iface_list *il)
{
  char *name;

  while (isspace(*buff)) buff++;
  name = strsep(&buff, ":");
  if(!buff)
    error_exit("error in getting the device name:");

  if(strlen(name) < (IFNAMSIZ)) {
    strncpy(il->ifrname, name, IFNAMSIZ-1);
    il->ifrname[IFNAMSIZ-1] = 0;
  } else il->ifrname[0] = 0;

  sscanf(buff, "%llu%llu%lu%lu%lu%lu%lu%lu%llu%llu%lu%lu%lu%lu%lu%lu",
    &il->receive_bytes, &il->receive_packets, &il->receive_errors,
    &il->receive_drop, &il->receive_fifo, &il->receive_frame,
    &il->receive_compressed, &il->receive_multicast, &il->transmit_bytes,
    &il->transmit_packets, &il->transmit_errors, &il->transmit_drop,
    &il->transmit_fifo, &il->transmit_colls, &il->transmit_carrier,
    &il->transmit_compressed);
}

static void add_iface_to_list(struct iface_list *newnode)
{
  struct iface_list *head_ref = TT.iface_list;

  if(!head_ref || strcmp(newnode->ifrname, head_ref->ifrname) < 0) {
    newnode->next = head_ref;
    head_ref = newnode;
  } else {
    struct iface_list *current = head_ref;
    while(current->next && strcmp(current->next->ifrname, newnode->ifrname) < 0)
      current = current->next;
    newnode->next = current->next;
    current->next = newnode;
  }
  TT.iface_list = (void *)head_ref;
}

static int get_device_info(struct iface_list *il)
{
  struct ifreq ifre;
  char *ifrname = il->ifrname;
  int sokfd;

  if ((sokfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return sokfd;
  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFFLAGS, &ifre) < 0) {
    close(sokfd);
    return NO_RANGE;
  }
  il->ifrflags = ifre.ifr_flags;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFHWADDR, &ifre) >= 0)
    memcpy(il->ifrhwaddr.sa_data, ifre.ifr_hwaddr.sa_data, sizeof(il->ifrhwaddr.sa_data));

  il->hw_type = ifre.ifr_hwaddr.sa_family;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMETRIC, &ifre) >= 0)
    il->ifrmetric = ifre.ifr_metric;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMTU, &ifre) >= 0)
    il->ifrmtu = ifre.ifr_mtu;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMAP, &ifre) == 0)
    il->ifrmap = ifre.ifr_map;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  il->txqueuelen = NO_RANGE;
  if(ioctl(sokfd, SIOCGIFTXQLEN, &ifre) >= 0)
    il->txqueuelen = ifre.ifr_qlen;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  ifre.ifr_addr.sa_family = AF_INET;

  if(!ioctl(sokfd, SIOCGIFADDR, &ifre)) {
    il->ifaddr = 1;
    il->ifraddr = ifre.ifr_addr;
    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFDSTADDR, &ifre) >= 0)
      il->ifrdstaddr = ifre.ifr_dstaddr;

    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFBRDADDR, &ifre) >= 0)
      il->ifrbroadaddr = ifre.ifr_broadaddr;

    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFNETMASK, &ifre) >= 0)
      il->ifrnetmask = ifre.ifr_netmask;
  }
  close(sokfd);
  return 0;
}

static void get_ifconfig_info(void)
{
  int i;
  FILE *fp;

  if (!(fp = fopen("/proc/net/dev", "r"))) return;

  for (i=0; fgets(toybuf, sizeof(toybuf), fp); i++) {
    struct iface_list *il;

    if (i<2) continue;

    il = xzalloc(sizeof(struct iface_list));
    get_proc_info(toybuf, il);
    add_iface_to_list(il);
    il->non_virtual_iface = 1;
    errno = 0;
    if(get_device_info(il) < 0) perror_exit("%s", il->ifrname);
  }
  fclose(fp);
}

static void get_hw_info(int hw_type, HW_INFO *hw_info)
{
  memset(hw_info, 0, sizeof(HW_INFO));

  switch(hw_type) {
    case ARPHRD_LOOPBACK: //Loopback device.
      strncpy(hw_info->hw_name, "loop", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "Local Loopback", HW_TITLE_LEN);
      hw_info->hw_addrlen = 0;
      break;
    case ARPHRD_ETHER: //Ethernet
      strncpy(hw_info->hw_name, "ether", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "Ethernet", HW_TITLE_LEN);
      hw_info->hw_addrlen = ETH_ALEN;
      break;
    case ARPHRD_PPP: //ARPHRD_PPP
      strncpy(hw_info->hw_name, "ppp", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "Point-to-Point Protocol", HW_TITLE_LEN);
      hw_info->hw_addrlen = 0;
      break;
    case ARPHRD_INFINIBAND: //InfiniBand
      strncpy(hw_info->hw_name, "infiniband", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "InfiniBand", HW_TITLE_LEN);
      hw_info->hw_addrlen = 20;
      break;
    case ARPHRD_SIT: //sit0 device - IPv6-in-IPv4
      strncpy(hw_info->hw_name, "sit", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "IPv6-in-IPv4", HW_TITLE_LEN);
      hw_info->hw_addrlen = 0;
      break;
    case -1:
      strncpy(hw_info->hw_name, "unspec", HW_NAME_LEN);
      strncpy(hw_info->hw_title, "UNSPEC", HW_TITLE_LEN);
      hw_info->hw_addrlen = 0;
      break;
    default:
      break;
  }
}

static void print_hw_addr(int hw_type, HW_INFO hw_info, struct iface_list *il)
{
  char *address = il->ifrhwaddr.sa_data;

  if(!address || !hw_info.hw_addrlen) return;
  xprintf("HWaddr ");
  if(hw_type == ARPHRD_ETHER) {
    int i;

    for (i=0; i<6; i++) xprintf(":%02X"+!i, address[i]);
  }
}

static char *get_ip_addr(struct sockaddr *skaddr)
{
  struct sockaddr_in *sin = (struct sockaddr_in *)skaddr;

  if(skaddr->sa_family == 0xFFFF || !skaddr->sa_family) return "[NOT SET]";
  if(sin->sin_family != AF_INET) {
    errno = EAFNOSUPPORT;
    return NULL;
  }

  return inet_ntoa(sin->sin_addr);
}

static void print_ip_addr(struct iface_list *il)
{
  char *af_name;
  int af = il->ifraddr.sa_family;

  if (af == AF_INET) af_name = "inet";
  else if (af == AF_INET6) af_name = "inet6";
  else if (af == AF_UNSPEC) af_name = "unspec";

  xprintf("%10s%s addr:%s ", " ", af_name, get_ip_addr(&il->ifraddr));
  if(il->ifrflags & IFF_POINTOPOINT)
    xprintf(" P-t-P:%s ", get_ip_addr(&il->ifrdstaddr));
  if(il->ifrflags & IFF_BROADCAST)
    xprintf(" Bcast:%s ", get_ip_addr(&il->ifrbroadaddr));
  xprintf(" Mask:%s\n", get_ip_addr(&il->ifrnetmask));
}

static void print_ip6_addr(struct iface_list *il)
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
    if(strcmp(il->ifrname, iface_name) == 0) {
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

static void display_ifconfig(struct iface_list *il)
{
  HW_INFO hw_info;

  get_hw_info(il->hw_type, &hw_info);
  xprintf("%-9s Link encap:%s  ", il->ifrname, hw_info.hw_title);
  print_hw_addr(il->hw_type, hw_info, il);
  xputc('\n');

  //print addr, p-p addr, broadcast addr and mask addr.
  if(il->ifaddr) print_ip_addr(il);

  //for ipv6 to do.
  print_ip6_addr(il);
  xprintf("%10s", " ");
  //print flags

  if (il->ifrflags) {
    unsigned short mask = 1;
    char **s, *str[] = {
      "UP", "BROADCAST", "DEBUG", "LOOPBACK", "POINTOPOINT", "NOTRAILERS",
      "RUNNING", "NOARP", "PROMISC", "ALLMULTI", "MASTER", "SLAVE", "MULTICAST",
      "PORTSEL", "AUTOMEDIA", "DYNAMIC", NULL
    };

    for(s = str; *s; s++) {
      if(il->ifrflags & mask) xprintf("%s ", *s);
      mask = mask << 1;
    }
  } else xprintf("[NO FLAGS] ");

  if(!il->ifrmetric) il->ifrmetric = 1;
  xprintf(" MTU:%d  Metric:%d\n", il->ifrmtu, il->ifrmetric);

  if(il->non_virtual_iface) {
    xprintf("%10cRX packets:%llu errors:%lu dropped:%lu overruns:%lu frame:%lu\n",
        ' ', il->receive_packets, il->receive_errors, il->receive_drop,
        il->receive_fifo, il->receive_frame);
    //Dummy types for non ARP hardware.
    if((il->hw_type == ARPHRD_CSLIP) || (il->hw_type == ARPHRD_CSLIP6))
      xprintf("%10ccompressed:%lu\n", ' ', il->receive_compressed);
    xprintf("%10cTX packets:%llu errors:%lu dropped:%lu overruns:%lu carrier:%lu\n", ' ',
        il->transmit_packets, il->transmit_errors, il->transmit_drop,
        il->transmit_fifo, il->transmit_carrier);
    xprintf("%10ccollisions:%lu ", ' ', il->transmit_colls);
    //Dummy types for non ARP hardware.
    if((il->hw_type == ARPHRD_CSLIP) || (il->hw_type == ARPHRD_CSLIP6))
      xprintf("compressed:%lu ", il->transmit_compressed);
    if(il->txqueuelen != NO_RANGE) xprintf("txqueuelen:%d ", il->txqueuelen);

    xprintf("\n%10cRX bytes:%llu TX bytes:%llu\n", ' ', il->receive_bytes,
      il->transmit_bytes);
  }
  if(il->ifrmap.irq || il->ifrmap.mem_start || il->ifrmap.dma || il->ifrmap.base_addr) {
    xprintf("%10c", ' ');
    if(il->ifrmap.irq) xprintf("Interrupt:%d ", il->ifrmap.irq);
    if(il->ifrmap.base_addr >= IO_MAP_INDEX)
      xprintf("Base address:0x%lx ", il->ifrmap.base_addr);
    if(il->ifrmap.mem_start)
      xprintf("Memory:%lx-%lx ", il->ifrmap.mem_start, il->ifrmap.mem_end);
    if(il->ifrmap.dma) xprintf("DMA chan:%x ", il->ifrmap.dma);
    xputc('\n');
  }
  xputc('\n');
}

static int readconf(void)
{
  int num_of_req = 30;
  struct ifconf ifcon;
  struct ifreq *ifre;
  int num, status = -1, sokfd;

  ifcon.ifc_buf = NULL;
  sokfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sokfd < 0) {
    perror_msg("error: no inet socket available");
    return -1;
  }
  for (;;) {
    ifcon.ifc_len = sizeof(struct ifreq) * num_of_req; //Size of buffer.
    ifcon.ifc_buf = xrealloc(ifcon.ifc_buf, ifcon.ifc_len);

    if((status = ioctl(sokfd, SIOCGIFCONF, &ifcon)) == -1) {
      perror_msg("ioctl %#x failed", SIOCGIFCONF);
      goto LOOP_BREAK;
    }
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
    struct iface_list *il;

    for(il = TT.iface_list; il; il = il->next)
      if(!strcmp(ifre->ifr_name, il->ifrname)) break;
    if(!il) {
      il = xzalloc(sizeof(struct iface_list));
      safe_strncpy(il->ifrname, ifre->ifr_name, IFNAMSIZ);
      add_iface_to_list(il);
      errno = 0;
      if(get_device_info(il) < 0) perror_exit("%s", il->ifrname);
    }
  }

LOOP_BREAK:
  close(sokfd);
  free(ifcon.ifc_buf);

  return status;
}

static int show_iface(char *iface_name)
{
  struct iface_list *il;

  get_ifconfig_info();

  if(iface_name) {
    for(il = TT.iface_list; il; il = il->next) {
      if(!strcmp(il->ifrname, iface_name)) {
        display_ifconfig(il);
        break;
      }
    }
    //if the given interface is not in the list.
    if(!il) {
      il = xzalloc(sizeof(struct iface_list));
      safe_strncpy(il->ifrname, iface_name, IFNAMSIZ);
      errno = 0;
      if(get_device_info(il) < 0) {
        perror_msg("%s", iface_name);
        free(il);
        return 1;
      } else display_ifconfig(il);
      free(il);
    }
  } else {
    if(readconf() < 0) return 1;
    for(il = TT.iface_list; il; il = il->next)
      if((il->ifrflags & IFF_UP) || (toys.optflags & FLAG_a))
        display_ifconfig(il);
  }
  return 0;
}

void ifconfig_main(void)
{
  char **argv = toys.optargs;

  if(*argv && (strcmp(*argv, "--help") == 0)) show_help();
  
  //"ifconfig" / "ifconfig eth0"
  if(!argv[0] || !argv[1]) { //one or no argument
    toys.exitval = show_iface(*argv);
    //free allocated memory.
    llist_traverse(TT.iface_list, free);
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
