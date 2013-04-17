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

#include <net/route.h>
#include <sys/un.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <alloca.h>

typedef struct sockaddr_with_len {
  union {
    struct sockaddr sock;
    struct sockaddr_in sock_in;
    struct sockaddr_in6 sock_in6;
  }sock_u;
  socklen_t socklen;
} sockaddr_with_len;

void setport(struct sockaddr *, unsigned);
unsigned get_strtou(char *, char **, int);
char *address_to_name(struct sockaddr *);
sockaddr_with_len *get_sockaddr(char *, int, sa_family_t);

typedef struct _proc_net_dev_info {
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
} PROC_NET_DEV_INFO;

// man netdevice
typedef struct _iface_list {
  int    hw_type;
  short   ifrflags; //used for addr, broadcast, and mask.
  short   ifaddr; //if set print ifraddr, irrdstaddr, ifrbroadaddr and ifrnetmask.
  struct sockaddr ifraddr;
  struct sockaddr ifrdstaddr;
  struct sockaddr ifrbroadaddr;
  struct sockaddr ifrnetmask;
  struct sockaddr ifrhwaddr;
  int    ifrmtu;
  int   ifrmetric;
  PROC_NET_DEV_INFO dev_info;
  int   txqueuelen;
  struct ifmap ifrmap;
  int non_virtual_iface;
  struct  _iface_list *next; //, *prev;
} IFACE_LIST;


#define HW_NAME_LEN 20
#define HW_TITLE_LEN 30

typedef struct _hw_info {
  char hw_name[HW_NAME_LEN];
  char hw_title[HW_TITLE_LEN];
  int     hw_addrlen;
} HW_INFO;

static char *field_format[] = {
  "%n%llu%u%u%u%u%n%n%n%llu%u%u%u%u%u",
  "%llu%llu%u%u%u%u%n%n%llu%llu%u%u%u%u%u",
  "%llu%llu%u%u%u%u%u%u%llu%llu%u%u%u%u%u%u"
};

#define NO_RANGE -1
#define IO_MAP_INDEX 0x100

static int show_iface(char *iface_name);
static void print_ip6_addr(IFACE_LIST *l_ptr);
static void clear_list(void);

//from /net/if.h
static char *iface_flags_str[] = {
      "UP",
      "BROADCAST",
      "DEBUG",
      "LOOPBACK",
      "POINTOPOINT",
      "NOTRAILERS",
      "RUNNING",
      "NOARP",
      "PROMISC",
      "ALLMULTI",
      "MASTER",
      "SLAVE",
      "MULTICAST",
      "PORTSEL",
      "AUTOMEDIA",
      "DYNAMIC",
      NULL
};
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

//==================================================================================
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

static void set_data(int sockfd, struct ifreq *ifre, char *kval, int request, char *req_name);
static void set_flags(int sockfd, struct ifreq *ifre, int arg_flag, int flag); //verify
static void set_mtu(int sockfd, struct ifreq *ifre, char *mtu); //verify
static void set_metric(int sockfd, struct ifreq *ifre, char *metric); //verify
static void set_qlen(int sockfd, struct ifreq *ifre, char *qlen); //verify
static void set_address(int sockfd, char *host_name, struct ifreq *ifre, int request, char *req_name);
static void set_hw_address(int sockfd, char ***argv, struct ifreq *ifre, int request, char *req_name);
static void set_ipv6_addr(int sockfd, struct ifreq *ifre, char *ipv6_addr, int request, char *req_name);
static void set_memstart(int sockfd, struct ifreq *ifre, char *start_addr, int request, char *req_name);
static void set_ioaddr(int sockfd, struct ifreq *ifre, char *baddr, int request, char *req_name);
static void set_irq(int sockfd, struct ifreq *ifre, char *irq_val, int request, char *req_name);

char *omit_whitespace(char *s)
{
  while(*s == ' ' || (unsigned char)(*s - 9) <= (13 - 9)) s++;
  return (char *) s;
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
  if(strncmp(host, "local:", 6) == 0) {
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
 * validate the input param (host) for valid ipv6 ip and extract port number (if there).
 */
static void get_host_and_port(char **host, int *port)
{
  char *ch_ptr;
  char *org_host = *host;
  if(*host[0] == '[') {
    (*host)++;
    ch_ptr = strchr(*host, ']');
    if(!ch_ptr || (ch_ptr[1] != ':' && ch_ptr[1] != '\0'))
      error_exit("bad address '%s'", org_host);
  }
  else {
    ch_ptr = strrchr(*host, ':');
    //There is more than one ':' like "::1"
    if(ch_ptr && strchr(*host, ':') != ch_ptr)
      ch_ptr = NULL;
  }
  if(ch_ptr) { //pointer to ":" or "]:"
    int size = ch_ptr - (*host) + 1;
    safe_strncpy(*host, *host, size);
    if(*ch_ptr != ':') {
      ch_ptr++; //skip ']'
      //[nn] without port
      if(!*ch_ptr) return;
    }
    ch_ptr++; //skip ':' to get the port number.
    *port = get_strtou(ch_ptr, NULL, 10);
    if(errno || (unsigned)*port > 65535)
      error_exit("bad port spec '%s'", org_host);
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
  if (status) perror_exit("bad address '%s' : %s", host, gai_strerror(status));

  for(rp = result; rp; rp = rp->ai_next) {
    if(rp->ai_family == AF_INET || rp->ai_family == AF_INET6) {
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
  }
  else if(sock->sa_family == AF_INET6) {
    socklen_t len = sizeof(struct sockaddr_in6);
    if((status = getnameinfo(sock, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) == 0) {
      //verification for resolved hostname.
      if(strchr(hbuf, ':')) return xmsprintf("[%s]:%s", hbuf, sbuf);
      else return xmsprintf("%s:%s", hbuf, sbuf);
    }
    else {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(status));
      return NULL;
    }
  }
  else if(sock->sa_family == AF_UNIX) {
    struct sockaddr_un *sockun = (void*)sock;
    return xmsprintf("local:%.*s", (int) sizeof(sockun->sun_path), sockun->sun_path);
  }
  else
    return NULL;
}

/*
 * used to set the port number for ipv4 / ipv6 addresses.
 */
void setport(struct sockaddr *sock, unsigned port_num)
{
  if(sock->sa_family == AF_INET)
    ((struct sockaddr_in *)sock)->sin_port = port_num;
  else if(sock->sa_family == AF_INET6)
    ((struct sockaddr_in6 *)sock)->sin6_port = port_num;
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


IFACE_LIST *iface_list_head;

void ifconfig_main(void)
{
  char **argv = toys.optargs;

  if(*argv && (strcmp(*argv, "--help") == 0))
    show_help();
  
  //"ifconfig" / "ifconfig eth0"
  if(!argv[0] || !argv[1]) { //one or no argument
    toys.exitval = show_iface(*argv);
    //free allocated memory.
    clear_list();
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
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      perror_exit("socket");

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
        show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFDSTADDR, "SIOCSIFDSTADDR");
        set_flags(sockfd, &ifre, IFF_POINTOPOINT, 0);
      } else if (!strcmp(*argv, "netmask")) {
        show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFNETMASK, "SIOCSIFNETMASK");
      } else if (!strcmp(*argv, "-broadcast")) {
        set_flags(sockfd, &ifre, 0, IFF_BROADCAST);
      } else if (!strcmp(*argv, "broadcast")) {
        show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFBRDADDR, "SIOCSIFBRDADDR");
        set_flags(sockfd, &ifre, IFF_BROADCAST, 0);
      } else if (!strcmp(*argv, "dstaddr")) {
        show_help();
        set_address(sockfd, *argv, &ifre, SIOCSIFDSTADDR, "SIOCSIFDSTADDR");
      } else if (!strcmp(*argv, "hw")) {
        show_help();
        set_hw_address(sockfd, &argv, &ifre, SIOCSIFHWADDR, "SIOCSIFHWADDR");
      } else if (!strcmp(*argv, "mtu")) {
        show_help();
        set_mtu(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "metric")) {
        show_help();
        set_metric(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "txqueuelen")) {
        show_help();
        set_qlen(sockfd, &ifre, *argv);
      } else if (!strcmp(*argv, "keepalive")) {
        show_help();
        set_data(sockfd, &ifre, *argv, SIOCSKEEPALIVE, "SIOCSKEEPALIVE");
      }//end of keepalive
      else if (!strcmp(*argv, "outfill")) {
        show_help();
        set_data(sockfd, &ifre, *argv, SIOCSOUTFILL, "SIOCSOUTFILL");
      } else if (!strcmp(*argv, "add")) {
        show_help();
        set_ipv6_addr(sockfd, &ifre, *argv, SIOCSIFADDR, "SIOCSIFADDR");
      } else if (!strcmp(*argv, "del")) {
        show_help();
        set_ipv6_addr(sockfd, &ifre, *argv, SIOCDIFADDR, "SIOCDIFADDR");
      } else if (!strcmp(*argv, "mem_start")) {
        show_help();
        set_memstart(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else if (!strcmp(*argv, "io_addr")) {
        show_help();
        set_ioaddr(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else if (!strcmp(*argv, "irq")) {
        show_help();
        set_irq(sockfd, &ifre, *argv, SIOCSIFMAP, "SIOCSIFMAP");
      } else {
        if(isdigit(**argv) || !strcmp(*argv, "default")) {
          char *iface_name = ifre.ifr_name;
          short int is_colon = 0;
          set_address(sockfd, *argv, &ifre, SIOCSIFADDR, "SIOCSIFADDR");
          while(*iface_name) {
            if(*iface_name == ':') {
              is_colon = 1;
              break;
            }
            iface_name++;
          }
          //if the interface name is not an alias; set the flag and continue.
          if(!is_colon)
            set_flags(sockfd, &ifre, IFF_UP | IFF_RUNNING, 0);
        } else if (!strcmp(*argv, "inet") || !strcmp(*argv, "inet6"))
          continue;
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


static void set_flags(int sockfd, struct ifreq *ifre, int set_flag, int reset_flag)
{
  if(ioctl(sockfd, SIOCGIFFLAGS, ifre) < 0)
    perror_exit("SIOCGIFFLAGS");
  ifre->ifr_flags = (ifre->ifr_flags & (~reset_flag)) | set_flag;
  if(ioctl(sockfd, SIOCSIFFLAGS, ifre) < 0)
    perror_exit("SIOCSIFFLAGS");
  return;
}

static void set_data(int sockfd, struct ifreq *ifre, char *kval, int request, char *req_name)
{
  unsigned long val = strtoul(kval, NULL, 0);
  char *ptr;
  ptr = ((char *) ifre) + offsetof(struct ifreq, ifr_data);
  (*(char * *)ptr) = (char *)val;

  if(ioctl(sockfd, request, ifre) < 0) {
    perror_exit((char *)req_name);
  }
  return;
}
static void set_mtu(int sockfd, struct ifreq *ifre, char *mtu)
{
  ifre->ifr_mtu = strtoul(mtu, NULL, 0);
  if(ioctl(sockfd, SIOCSIFMTU, ifre) < 0)
    perror_exit("SIOCSIFMTU");
  return;
}

static void set_metric(int sockfd, struct ifreq *ifre, char *metric)
{
  ifre->ifr_metric = strtoul(metric, NULL, 0);
  if(ioctl(sockfd, SIOCSIFMETRIC, ifre) < 0)
    perror_exit("SIOCSIFMETRIC");
  return;
}

static void set_qlen(int sockfd, struct ifreq *ifre, char *qlen)
{
  ifre->ifr_qlen = strtoul(qlen, NULL, 0);
  if(ioctl(sockfd, SIOCSIFTXQLEN, ifre) < 0)
    perror_exit("SIOCSIFTXQLEN");
  return;
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
    if(ioctl(sockfd6, SIOGIFINDEX, ifre) < 0)
      perror_exit("SIOGIFINDEX");
    ifre6.ifrinet6_ifindex = ifre->ifr_ifindex;
    ifre6.ifrinet6_prefixlen = plen;

    if(ioctl(sockfd6, request, &ifre6) < 0)
      perror_exit((char *)req_name);
    if(swl != NULL) {
      free(swl);
      swl = NULL;
    }
  return;
}

static void set_address(int sockfd, char *host_name, struct ifreq *ifre, int request, char *req_name)
{
  struct sockaddr_in sock_in;
  sockaddr_with_len *swl = NULL;
  sock_in.sin_family = AF_INET;
  sock_in.sin_port = 0;

  //Default 0.0.0.0
  if(strcmp(host_name, "default") == 0)
    sock_in.sin_addr.s_addr = INADDR_ANY;
  else {
    swl = get_sockaddr(host_name, 0, AF_INET);
    if(!swl) error_exit("error in resolving host name");

    sock_in.sin_addr = swl->sock_u.sock_in.sin_addr;
  }
  memcpy((char *)&ifre->ifr_addr, (char *) &sock_in, sizeof(struct sockaddr));
  if(ioctl(sockfd, request, ifre) < 0)
    perror_exit((char *)req_name);

  if(swl != NULL) {
    free(swl);
    swl = NULL;
  }
  return;
}

static int hex_to_binary(char *hw_addr, struct sockaddr *sock, int count)
{
  int i = 0, j = 0;
  unsigned char nib_val;
  unsigned char ch;

  char *ptr = (char *) sock->sa_data;
  if(count == ETH_ALEN)
    sock->sa_family = ARPHRD_ETHER;
  else if(count == INFINIBAND_ALEN)
    sock->sa_family = ARPHRD_INFINIBAND;
  else
    return -1;
  //e.g. hw_addr "62:2D:A6:9E:2D:BE"
  for(; *hw_addr && (i < count); i++) {
    if(*hw_addr == ':')
      hw_addr++;
    j = nib_val = 0;
    for(;j < 2; j++) {
      ch = *hw_addr;
      //0-9 = 10 chars.
      if(((unsigned char)(ch - '0')) < 10)
        ch = (ch - '0');
      //a-f = 6 chars.
      else if(((unsigned char)((ch) - 'a')) < 6)
        ch = (ch - ('a'-10));
      //A-F = 6 chars.
      else if(((unsigned char)((ch) - 'A')) < 6)
        ch = (ch - ('A'-10));
      else if(j && (ch == ':' || ch == 0))
        break;
      else
        return -1;
      hw_addr++;
      nib_val <<= 4;
      nib_val += ch;
    }
    *ptr++ = nib_val;
  }
  if(*hw_addr)
    return -1;
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
  if(hw_class == 1) {
    if(hex_to_binary(hw_addr, &sock, ETH_ALEN))
      error_exit("invalid hw-addr %s", hw_addr);
  }
  else {
    if(hex_to_binary(hw_addr, &sock, INFINIBAND_ALEN))
      error_exit("invalid hw-addr %s", hw_addr);
  }
  ptr = (char *)&sock;
  memcpy( ((char *) ifre) + offsetof(struct ifreq, ifr_hwaddr), ptr, sizeof(struct sockaddr));
  if(ioctl(sockfd, request, ifre) < 0)
    perror_exit((char *)req_name);
  return;
}

static void set_memstart(int sockfd, struct ifreq *ifre, char *start_addr, int request, char *req_name)
{
  unsigned long mem_start = strtoul(start_addr, NULL, 0);

  if(ioctl(sockfd, SIOCGIFMAP, ifre) < 0)
    perror_exit("SIOCGIFMAP");
  ifre->ifr_map.mem_start = mem_start;
  if(ioctl(sockfd, request, ifre) < 0)
    perror_exit((char *)req_name);
  return;
}

static void set_ioaddr(int sockfd, struct ifreq *ifre, char *baddr, int request, char *req_name)
{
  unsigned short int base_addr = strtoul(baddr, NULL, 0);
  if(ioctl(sockfd, SIOCGIFMAP, ifre) < 0)
    perror_exit("SIOCGIFMAP");
  ifre->ifr_map.base_addr = base_addr;
  if(ioctl(sockfd, request, ifre) < 0)
    perror_exit((char *)req_name);
  return;
}

static void set_irq(int sockfd, struct ifreq *ifre, char *irq_val, int request, char *req_name)
{
  unsigned short int irq = strtoul(irq_val, NULL, 0);
  char *ptr;
  struct ifmap *map;

  if(ioctl(sockfd, SIOCGIFMAP, ifre) < 0) perror_exit("SIOCGIFMAP");

  ptr = ((char *) ifre) + offsetof(struct ifreq, ifr_map);
  map = (struct ifmap *)ptr;
  map->irq = irq;
  if(ioctl(sockfd, request, ifre) < 0) perror_exit(req_name);
  return;
}

/* Display ifconfig info. */
static void get_proc_info(char *buff, IFACE_LIST *l_ptr, int version)
{
  char *name;
  memset(&l_ptr->dev_info, 0, sizeof(PROC_NET_DEV_INFO));

  buff = omit_whitespace(buff);
  name = strsep(&buff, ":");
  if(!buff)
    error_exit("error in getting the device name:");

  if(strlen(name) < (IFNAMSIZ)) {
    strncpy(l_ptr->dev_info.ifrname, name, IFNAMSIZ-1);
    l_ptr->dev_info.ifrname[IFNAMSIZ-1] = '\0';
  }
  else {
    l_ptr->dev_info.ifrname[0] = '\0';
  }

  sscanf(buff, field_format[version],
      &l_ptr->dev_info.receive_bytes,
      &l_ptr->dev_info.receive_packets,
      &l_ptr->dev_info.receive_errors,
      &l_ptr->dev_info.receive_drop,
      &l_ptr->dev_info.receive_fifo,
      &l_ptr->dev_info.receive_frame,
      &l_ptr->dev_info.receive_compressed,
      &l_ptr->dev_info.receive_multicast,
      &l_ptr->dev_info.transmit_bytes,
      &l_ptr->dev_info.transmit_packets,
      &l_ptr->dev_info.transmit_errors,
      &l_ptr->dev_info.transmit_drop,
      &l_ptr->dev_info.transmit_fifo,
      &l_ptr->dev_info.transmit_colls,
      &l_ptr->dev_info.transmit_carrier,
      &l_ptr->dev_info.transmit_compressed
    );

  if(version == 0)
    l_ptr->dev_info.receive_bytes = l_ptr->dev_info.transmit_bytes = 0;
  if(version == 1)
    l_ptr->dev_info.receive_multicast = l_ptr->dev_info.receive_compressed = l_ptr->dev_info.transmit_compressed = 0;
  return;
}

static void add_iface_to_list(IFACE_LIST *newnode)
{
  IFACE_LIST *head_ref = iface_list_head;

  if((head_ref == NULL) || strcmp(newnode->dev_info.ifrname, head_ref->dev_info.ifrname) < 0) {
    newnode->next = head_ref;
    head_ref = newnode;
  }
  else {
    IFACE_LIST *current = head_ref;
    while(current->next != NULL && (strcmp(current->next->dev_info.ifrname, newnode->dev_info.ifrname)) < 0)
      current = current->next;
    newnode->next = current->next;
    current->next = newnode;
  }
  iface_list_head = head_ref;
}

static int get_device_info(IFACE_LIST *l_ptr)
{
  struct ifreq ifre;
  char *ifrname = l_ptr->dev_info.ifrname;
  int sokfd;

  sokfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sokfd < 0)
    return sokfd;
  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFFLAGS, &ifre) < 0) {
    close(sokfd);
    return NO_RANGE;
  }
  l_ptr->ifrflags = ifre.ifr_flags;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFHWADDR, &ifre) >= 0)
    memcpy(l_ptr->ifrhwaddr.sa_data, ifre.ifr_hwaddr.sa_data, sizeof(l_ptr->ifrhwaddr.sa_data));

  l_ptr->hw_type = ifre.ifr_hwaddr.sa_family;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMETRIC, &ifre) >= 0)
    l_ptr->ifrmetric = ifre.ifr_metric;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMTU, &ifre) >= 0)
    l_ptr->ifrmtu = ifre.ifr_mtu;

#ifdef SIOCGIFMAP
  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  if(ioctl(sokfd, SIOCGIFMAP, &ifre) == 0)
    l_ptr->ifrmap = ifre.ifr_map;
#endif

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  l_ptr->txqueuelen = NO_RANGE;
  if(ioctl(sokfd, SIOCGIFTXQLEN, &ifre) >= 0)
    l_ptr->txqueuelen = ifre.ifr_qlen;

  strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
  ifre.ifr_addr.sa_family = AF_INET;

  if(ioctl(sokfd, SIOCGIFADDR, &ifre) == 0) {
    l_ptr->ifaddr = 1;
    l_ptr->ifraddr = ifre.ifr_addr;
    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFDSTADDR, &ifre) >= 0)
      l_ptr->ifrdstaddr = ifre.ifr_dstaddr;

    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFBRDADDR, &ifre) >= 0)
      l_ptr->ifrbroadaddr = ifre.ifr_broadaddr;

    strncpy(ifre.ifr_name, ifrname, IFNAMSIZ);
    if(ioctl(sokfd, SIOCGIFNETMASK, &ifre) >= 0)
      l_ptr->ifrnetmask = ifre.ifr_netmask;
  }
  close(sokfd);
  return 0;
}

static void get_ifconfig_info(void)
{
  IFACE_LIST *l_ptr;
  char buff[BUFSIZ] = {0,};
  int version_num = 0;

  FILE *fp = fopen("/proc/net/dev", "r");
  if(fp == NULL)
	  return;

  fgets(buff, sizeof(buff), fp); //skip 1st header line.
  fgets(buff, sizeof(buff), fp); //skip 2nd header line.

  if(strstr(buff, "compressed"))
    version_num = 2;
  else if(strstr(buff, "bytes"))
    version_num = 1;
  else
    version_num = 0;

  while(fgets(buff, BUFSIZ, fp)) {
    l_ptr = xzalloc(sizeof(IFACE_LIST));
    get_proc_info(buff, l_ptr, version_num);
    add_iface_to_list(l_ptr);
    l_ptr->non_virtual_iface = 1;
    errno = 0;
    if(get_device_info(l_ptr) < 0) {
      char *errstr = strerror(errno);
      fclose(fp);
      fp = NULL;
      clear_list();
      perror_exit("%s: error getting interface info: %s", l_ptr->dev_info.ifrname, errstr);
    }
  }//end of while.
  fclose(fp);
  fp = NULL;
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

static void print_hw_addr(int hw_type, HW_INFO hw_info, IFACE_LIST *l_ptr)
{
  unsigned char *address = (unsigned char *)l_ptr->ifrhwaddr.sa_data;

  if(!address || !hw_info.hw_addrlen) return;
  xprintf("HWaddr ");
  if(hw_type == ARPHRD_ETHER) {
    int i;

    for (i=0; i<6; i++) xprintf(":%02X"+!i, address[i]);
  }

  return;
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

static void print_ip_addr(IFACE_LIST *l_ptr)
{
  char *af_name;
  int af = l_ptr->ifraddr.sa_family;

  if (af == AF_INET) af_name = "inet";
  else if (af == AF_INET6) af_name = "inet6";
  else if (af == AF_UNSPEC) af_name = "unspec";

  xprintf("%10s%s addr:%s ", " ", af_name, get_ip_addr(&l_ptr->ifraddr));
  if(l_ptr->ifrflags & IFF_POINTOPOINT)
    xprintf(" P-t-P:%s ", get_ip_addr(&l_ptr->ifrdstaddr));
  if(l_ptr->ifrflags & IFF_BROADCAST)
    xprintf(" Bcast:%s ", get_ip_addr(&l_ptr->ifrbroadaddr));
  xprintf(" Mask:%s\n", get_ip_addr(&l_ptr->ifrnetmask));
  return;
}

static void print_iface_flags(IFACE_LIST *l_ptr)
{
  if (l_ptr->ifrflags != 0) {
    unsigned short mask = 1;
    char **str = iface_flags_str;

    for(; *str != NULL; str++) {
      if(l_ptr->ifrflags & mask)
        xprintf("%s ", *str);
      mask = mask << 1;
    }
  } else xprintf("[NO FLAGS] ");
  return;
}

static void print_media(IFACE_LIST *l_ptr)
{
#ifdef IFF_PORTSEL
  if(l_ptr->ifrflags & IFF_PORTSEL) {
    xprintf("Media:");
    if(l_ptr->ifrmap.port == IF_PORT_UNKNOWN)
      xprintf("%s", "unknown");
    else if(l_ptr->ifrmap.port == IF_PORT_10BASE2)
      xprintf("%s", "10base2");
    else if(l_ptr->ifrmap.port == IF_PORT_10BASET)
      xprintf("%s", "10baseT");
    else if(l_ptr->ifrmap.port == IF_PORT_AUI)
      xprintf("%s", "AUI");
    else if(l_ptr->ifrmap.port == IF_PORT_100BASET)
      xprintf("%s", "100baseT");
    else if(l_ptr->ifrmap.port == IF_PORT_100BASETX)
      xprintf("%s", "100baseTX");
    else if(l_ptr->ifrmap.port == IF_PORT_100BASEFX)
      xprintf("%s", "100baseFX");
    if(l_ptr->ifrflags & IFF_AUTOMEDIA)
      xprintf("(auto)");
  }
#endif
  return;
}

static void print_ip6_addr(IFACE_LIST *l_ptr)
{
  char iface_name[IFNAMSIZ] = {0,};
  char buf[BUFSIZ] = {0,};
  int plen, scope;

  FILE *fp = fopen("/proc/net/if_inet6", "r");
  if(fp == NULL)
	  return;

  while(fgets(buf, BUFSIZ, fp)) {
    int nitems = 0;
    char ipv6_addr[40] = {0,};
    nitems = sscanf(buf, "%32s %*08x %02x %02x %*02x %15s\n",
        ipv6_addr+7, &plen, &scope, iface_name);
    if(nitems != 4) {
      if((nitems < 0) && feof(fp))
        break;
      perror_exit("sscanf");
    }
    if(strcmp(l_ptr->dev_info.ifrname,iface_name) == 0) {
      int i = 0;
      struct sockaddr_in6 sock_in6;
      int len = sizeof(ipv6_addr) / (sizeof ipv6_addr[0]);
      char *ptr = ipv6_addr+7;
      while((i < len-2) && (*ptr)) {
        ipv6_addr[i++] = *ptr++;
        //put ':' after 4th bit
        if(!((i+1) % 5))
          ipv6_addr[i++] = ':';
      }
      ipv6_addr[i+1] = '\0';
      if(inet_pton(AF_INET6, ipv6_addr, (struct sockaddr *) &sock_in6.sin6_addr) > 0) {
        sock_in6.sin6_family = AF_INET6;
        memset(buf, 0, (sizeof(buf) /sizeof(buf[0])));
        if(inet_ntop(AF_INET6, &sock_in6.sin6_addr, buf, BUFSIZ) > 0) {
          xprintf("%10sinet6 addr: %s/%d", " ", buf, plen);
          xprintf(" Scope:");
          if(scope == IPV6_ADDR_ANY) xprintf(" Global");
          else if(scope == IPV6_ADDR_LOOPBACK) xprintf(" Host");
          else if(scope == IPV6_ADDR_LINKLOCAL) xprintf(" Link");
          else if(scope == IPV6_ADDR_SITELOCAL) xprintf(" Site");
          else if(scope == IPV6_ADDR_COMPATv4) xprintf(" Compat");
          else xprintf("Unknown");
          xprintf("\n");
        }
      }
    }
  }//end of  while.
  fclose(fp);
  fp = NULL;
  return;
}

static void display_ifconfig(IFACE_LIST *l_ptr)
{
  HW_INFO hw_info;
  int hw_type = l_ptr->hw_type;

  get_hw_info(hw_type, &hw_info);
  xprintf("%-9s Link encap:%s  ", l_ptr->dev_info.ifrname, hw_info.hw_title);
  print_hw_addr(hw_type, hw_info, l_ptr);

  print_media(l_ptr);

  xputc('\n');
  if(l_ptr->ifaddr)
    print_ip_addr(l_ptr); //print addr, p-p addr, broadcast addr and mask addr.

  //for ipv6 to do.
  print_ip6_addr(l_ptr);
  xprintf("%10s", " ");
  //print flags
  print_iface_flags(l_ptr);
  if(!l_ptr->ifrmetric) l_ptr->ifrmetric = 1;
  xprintf(" MTU:%d  Metric:%d", l_ptr->ifrmtu, l_ptr->ifrmetric);
  xprintf("\n");
  if(l_ptr->non_virtual_iface) {
    xprintf("%10s", " ");
    xprintf("RX packets:%llu errors:%lu dropped:%lu overruns:%lu frame:%lu\n",
        l_ptr->dev_info.receive_packets, l_ptr->dev_info.receive_errors,
        l_ptr->dev_info.receive_drop, l_ptr->dev_info.receive_fifo,
        l_ptr->dev_info.receive_frame);
    //Dummy types for non ARP hardware.
    if((hw_type == ARPHRD_CSLIP) || (hw_type == ARPHRD_CSLIP6))
      xprintf("%10scompressed:%lu\n", " ", l_ptr->dev_info.receive_compressed);
    xprintf("%10sTX packets:%llu errors:%lu dropped:%lu overruns:%lu carrier:%lu\n", " ",
        l_ptr->dev_info.transmit_packets, l_ptr->dev_info.transmit_errors,
        l_ptr->dev_info.transmit_drop, l_ptr->dev_info.transmit_fifo,
        l_ptr->dev_info.transmit_carrier);
    xprintf("%10scollisions:%lu ", " ", l_ptr->dev_info.transmit_colls);
    //Dummy types for non ARP hardware.
    if((hw_type == ARPHRD_CSLIP) || (hw_type == ARPHRD_CSLIP6))
      xprintf("compressed:%lu ", l_ptr->dev_info.transmit_compressed);
    if(l_ptr->txqueuelen != NO_RANGE)
      xprintf("txqueuelen:%d ", l_ptr->txqueuelen);

    xprintf("\n%10s", " ");
    xprintf("RX bytes:%llu ", l_ptr->dev_info.receive_bytes);
    xprintf("TX bytes:%llu\n", l_ptr->dev_info.transmit_bytes);
  }
  if(l_ptr->ifrmap.irq || l_ptr->ifrmap.mem_start || l_ptr->ifrmap.dma || l_ptr->ifrmap.base_addr) {
    xprintf("%10s", " ");
    if(l_ptr->ifrmap.irq)
      xprintf("Interrupt:%d ", l_ptr->ifrmap.irq);
    if(l_ptr->ifrmap.base_addr >= IO_MAP_INDEX)
      xprintf("Base address:0x%lx ", l_ptr->ifrmap.base_addr);
    if(l_ptr->ifrmap.mem_start)
      xprintf("Memory:%lx-%lx ", l_ptr->ifrmap.mem_start, l_ptr->ifrmap.mem_end);
    if(l_ptr->ifrmap.dma)
      xprintf("DMA chan:%x ", l_ptr->ifrmap.dma);
    xputc('\n');
  }
  xputc('\n');
  return;
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
	}//End of while loop

	ifre = ifcon.ifc_req;
	for(num = 0; num < ifcon.ifc_len && ifre; num += sizeof(struct ifreq), ifre++) {
		//Escape duplicate values from the list.
		IFACE_LIST *list_ptr;
		int match_found = 0;
		for(list_ptr = iface_list_head; list_ptr != NULL; list_ptr = list_ptr->next) {
			//if interface already in the list then donot add it in the list.
			if(!strcmp(ifre->ifr_name, list_ptr->dev_info.ifrname)) {
				match_found = 1;
				break;
			}
		}
		if(!match_found) {
			IFACE_LIST *l_ptr = xzalloc(sizeof(IFACE_LIST));
			safe_strncpy(l_ptr->dev_info.ifrname, ifre->ifr_name, IFNAMSIZ);
			add_iface_to_list(l_ptr);
			errno = 0;
			if(get_device_info(l_ptr) < 0) {
			  clear_list();
			  perror_exit("%s: error getting interface info: %s", l_ptr->dev_info.ifrname, strerror(errno));
			}
		}
	}//End of for loop.

LOOP_BREAK:
	close(sokfd);
	free(ifcon.ifc_buf);

	return status;
}

static int show_iface(char *iface_name)
{
  get_ifconfig_info();

  if(iface_name) {
    IFACE_LIST *l_ptr;
    int is_dev_found = 0;
    for(l_ptr = iface_list_head; l_ptr; l_ptr = l_ptr->next) {
      if(strcmp(l_ptr->dev_info.ifrname, iface_name) == 0) {
        is_dev_found = 1;
        display_ifconfig(l_ptr);
        break;
      }
    }
    //if the given interface is not in the list.
    if(!is_dev_found) {
      IFACE_LIST *l_ptr = xzalloc(sizeof(IFACE_LIST));
      safe_strncpy(l_ptr->dev_info.ifrname, iface_name, IFNAMSIZ);
      errno = 0;
      if(get_device_info(l_ptr) < 0) {
        char *errmsg;
        if(errno == ENODEV) errmsg = "Device not found";
        else errmsg = strerror(errno);
        error_msg("%s: error getting interface info: %s", iface_name, errmsg);
        free(l_ptr);
        return 1;
      }
      else display_ifconfig(l_ptr);
      free(l_ptr);
    }
  } else {
    IFACE_LIST *l_ptr;
    if(readconf() < 0) return 1;
    for(l_ptr = iface_list_head; l_ptr; l_ptr = l_ptr->next) {
      if((l_ptr->ifrflags & IFF_UP) || (toys.optflags & FLAG_a))
        display_ifconfig(l_ptr);
    }
  }
  return 0;
}

static void clear_list(void)
{
  IFACE_LIST *temp_ptr;
  while(iface_list_head != NULL) {
    temp_ptr = iface_list_head->next;
    free(iface_list_head);
    iface_list_head = temp_ptr;
  }
  return;
}
