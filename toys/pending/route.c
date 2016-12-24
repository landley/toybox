/* route.c - Display/edit network routing table.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard
 *
 * TODO: autodetect -net -host target dev -A (but complain)
 * route add -net target 10.0.0.0 netmask 255.0.0.0 dev eth0
 * route del delete
 * delete net route, must match netmask, informative error message
 *
 * mod dyn reinstate metric netmask gw mss window irtt dev

USE_ROUTE(NEWTOY(route, "?neA:", TOYFLAG_BIN))
config ROUTE
  bool "route"
  default n
  help
    usage: route [-ne] [-A [46]] [add|del TARGET [OPTIONS]]

    Display, add or delete network routes in the "Forwarding Information Base".

    -n	Show numerical addresses (no DNS lookups)
    -e	display netstat fields

    Routing means sending packets out a network interface to an address.
    The kernel can tell where to send packets one hop away by examining each
    interface's address and netmask, so the most common use of this command
    is to identify a "gateway" that forwards other traffic.

    Assigning an address to an interface automatically creates an appropriate
    network route ("ifconfig eth0 10.0.2.15/8" does "route add 10.0.0.0/8 eth0"
    for you), although some devices (such as loopback) won't show it in the
    table. For machines more than one hop away, you need to specify a gateway
    (ala "route add default gw 10.0.2.2").

    The address "default" is a wildcard address (0.0.0.0/0) matching all
    packets without a more specific route.

    Available OPTIONS include:
    reject   - blocking route (force match failure)
    dev NAME - force packets out this interface (ala "eth0")
    netmask  - old way of saying things like ADDR/24
    gw ADDR  - forward packets to gateway ADDR

*/

#define FOR_route
#include "toys.h"
#include <net/route.h>

GLOBALS(
  char *family;
)

#define DEFAULT_PREFIXLEN 128
#define INVALID_ADDR 0xffffffffUL
#define IPV6_ADDR_LEN 40 //32 + 7 (':') + 1 ('\0')

struct _arglist {
  char *arg;

  int action;
};

static struct _arglist arglist1[] = {
  { "add", 1 }, { "del", 2 },
  { "delete", 2 }, { NULL, 0 }
};

static struct _arglist arglist2[] = {
  { "-net", 1 }, { "-host", 2 },
  { NULL, 0 }
};

// to get the host name from the given ip.
static int get_hostname(char *ipstr, struct sockaddr_in *sockin)
{
  struct hostent *host;

  sockin->sin_family = AF_INET;
  sockin->sin_port = 0;

  if (!strcmp(ipstr, "default")) {
    sockin->sin_addr.s_addr = INADDR_ANY;
    return 1;
  }

  if (inet_aton(ipstr, &sockin->sin_addr)) return 0;
  if (!(host = gethostbyname(ipstr))) perror_exit("resolving '%s'", ipstr);
  memcpy(&sockin->sin_addr, host->h_addr_list[0], sizeof(struct in_addr));

  return 0;
}

// used to extract the address info from the given ip.
static int get_addrinfo(char *ip, struct sockaddr_in6 *sock_in6)
{
  struct addrinfo hints, *result;
  int status = 0;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET6;
  if ((status = getaddrinfo(ip, NULL, &hints, &result))) {
    perror_msg("getaddrinfo: %s", gai_strerror(status));
    return -1;
  }
  if (result) {
    memcpy(sock_in6, result->ai_addr, sizeof(*sock_in6));
    freeaddrinfo(result);
  }
  return 0;
}

static void get_flag_value(char *str, int flags)
{
  // RTF_* bits in order:
  // UP, GATEWAY, HOST, REINSTATE, DYNAMIC, MODIFIED, DEFAULT, ADDRCONF, CACHE
  int i = 0, mask = 0x105003f;

  for (; mask; mask>>=1) if (mask&1) {
    if (flags&(1<<i)) *str++ = "UGHRDMDAC"[i];
    i++;
  }
  *str = 0;
}

// extract inet4 route info from /proc/net/route file and display it.
static void display_routes(void)
{
  unsigned long dest, gate, mask;
  int flags, ref, use, metric, mss, win, irtt, items;
  char iface[64] = {0,}, flag_val[10]; //there are 9 flags "UGHRDMDAC" for route.

  FILE *fp = xfopen("/proc/net/route", "r");

  xprintf("Kernel IP routing table\n"
      "Destination     Gateway         Genmask         Flags %s Iface\n",
      (toys.optflags & FLAG_e)? "  MSS Window  irtt" : "Metric Ref    Use");

  if (fscanf(fp, "%*[^\n]\n") < 0) perror_exit("fscanf"); //skip 1st line
  while ((items = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n", iface, &dest, 
          &gate, &flags, &ref, &use, &metric, &mask, &mss, &win, &irtt)) == 11)
  {
    char *destip = toybuf, *gateip = toybuf+32, *maskip = toybuf+64; //ip string 16

    if (!(flags & RTF_UP)) continue; //skip down interfaces.

    if (!dest && !(toys.optflags & FLAG_n)) strcpy( destip, "default");
    else if (!inet_ntop(AF_INET, &dest, destip, 32)) perror_exit("inet");

    if (!gate && !(toys.optflags & FLAG_n)) strcpy( gateip, "*");
    else if (!inet_ntop(AF_INET, &gate, gateip, 32)) perror_exit("inet");

    if (!inet_ntop(AF_INET, &mask, maskip, 32)) perror_exit("inet");

    //Get flag Values
    get_flag_value(flag_val, flags);
    if (flags & RTF_REJECT) flag_val[0] = '!';
    xprintf("%-15.15s %-15.15s %-16s%-6s", destip, gateip, maskip, flag_val);
    if (toys.optflags & FLAG_e) xprintf("%5d %-5d %6d %s\n", mss, win, irtt, iface);
    else xprintf("%-6d %-2d %7d %s\n", metric, ref, use, iface);
  }

  if (items > 0 && feof(fp)) perror_exit("fscanf %d", items);
  fclose(fp);
}

/*
 * find the given parameter in list like add/del/net/host.
 * and if match found return the appropriate action.
 */
static int get_action(char ***argv, struct _arglist *list)
{
  struct _arglist *alist;

  if (!**argv) return 0;
  for (alist = list; alist->arg; alist++) { //find the given parameter in list
    if (!strcmp(**argv, alist->arg)) {
      *argv += 1;
      return alist->action;
    }
  }
  return 0;
}

/*
 * used to get the params like: metric, netmask, gw, mss, window, irtt, dev and their values.
 * additionally set the flag values for reject, mod, dyn and reinstate.
 */
static void get_next_params(char **argv, struct rtentry *rt, char **netmask)
{
  for (;*argv;argv++) {
    if (!strcmp(*argv, "reject")) rt->rt_flags |= RTF_REJECT;
    else if (!strcmp(*argv, "mod")) rt->rt_flags |= RTF_MODIFIED;
    else if (!strcmp(*argv, "dyn")) rt->rt_flags |= RTF_DYNAMIC;
    else if (!strcmp(*argv, "reinstate")) rt->rt_flags |= RTF_REINSTATE;
    else {
      if (!argv[1]) help_exit(0);

      //set the metric field in the routing table.
      if (!strcmp(*argv, "metric"))
        rt->rt_metric = atolx_range(*argv, 0, ULONG_MAX) + 1;
      else if (!strcmp(*argv, "netmask")) {
        //when adding a network route, the netmask to be used.
        struct sockaddr sock;
        unsigned int addr_mask = (((struct sockaddr_in *)&((rt)->rt_genmask))->sin_addr.s_addr);

        if (addr_mask) help_exit("dup netmask");
        *netmask = *argv;
        get_hostname(*netmask, (struct sockaddr_in *) &sock);
        rt->rt_genmask = sock;
      } else if (!strcmp(*argv, "gw")) { 
        //route packets via a gateway.
        if (!(rt->rt_flags & RTF_GATEWAY)) {
          if (!get_hostname(*argv, (struct sockaddr_in *) &rt->rt_gateway))
            rt->rt_flags |= RTF_GATEWAY;
          else perror_exit("gateway '%s' is a NETWORK", *argv);
        } else help_exit("dup gw");
      } else if (!strcmp(*argv, "mss")) {
        //set the TCP Maximum Segment Size for connections over this route.
        rt->rt_mtu = atolx_range(*argv, 64, 65536);
        rt->rt_flags |= RTF_MSS;
      } else if (!strcmp(*argv, "window")) {
        //set the TCP window size for connections over this route to W bytes.
        rt->rt_window = atolx_range(*argv, 128, INT_MAX); //win low
        rt->rt_flags |= RTF_WINDOW;
      } else if (!strcmp(*argv, "irtt")) {
        rt->rt_irtt = atolx_range(*argv, 0, INT_MAX);
        rt->rt_flags |= RTF_IRTT;
      } else if (!strcmp(*argv, "dev") && !rt->rt_dev) rt->rt_dev = *argv;
      else help_exit("no '%s'", *argv);
    }
  }

  if (!rt->rt_dev && (rt->rt_flags & RTF_REJECT)) rt->rt_dev = (char *)"lo";
}

// verify the netmask and conflict in netmask and route address.
static void verify_netmask(struct rtentry *rt, char *netmask)
{
  unsigned int addr_mask = (((struct sockaddr_in *)&((rt)->rt_genmask))->sin_addr.s_addr);
  unsigned int router_addr = ~(unsigned int)(((struct sockaddr_in *)&((rt)->rt_dst))->sin_addr.s_addr);

  if (addr_mask) {
    addr_mask = ~ntohl(addr_mask);
    if ((rt->rt_flags & RTF_HOST) && addr_mask != INVALID_ADDR)
      perror_exit("conflicting netmask and host route");
    if (addr_mask & (addr_mask + 1)) perror_exit("wrong netmask '%s'", netmask);
    addr_mask = ((struct sockaddr_in *) &rt->rt_dst)->sin_addr.s_addr;
    if (addr_mask & router_addr) perror_exit("conflicting netmask and route address");
  }
}

// add/del a route.
static void setroute(char **argv)
{
  struct rtentry rt;
  char *netmask, *targetip;
  int is_net_or_host = 0, sokfd, arg2_action;
  int action = get_action(&argv, arglist1); //verify the arg for add/del.

  if (!action || !*argv) help_exit("setroute");

  arg2_action = get_action(&argv, arglist2); //verify the arg for -net or -host
  if (!*argv) help_exit("setroute");

  memset(&rt, 0, sizeof(struct rtentry));
  targetip = *argv++;

  netmask = strchr(targetip, '/');
  if (netmask) {
    *netmask++ = 0;
    //used to verify the netmask and route conflict.
    (((struct sockaddr_in *)&rt.rt_genmask)->sin_addr.s_addr)
      = htonl((1<<(32-atolx_range(netmask, 0, 32)))-1);
    rt.rt_genmask.sa_family = AF_INET;
    netmask = 0;
  } else netmask = "default";

  is_net_or_host = get_hostname(targetip, (void *)&rt.rt_dst);

  if (arg2_action) is_net_or_host = arg2_action & 1;
  rt.rt_flags = ((is_net_or_host) ? RTF_UP : (RTF_UP | RTF_HOST));

  get_next_params(argv, &rt, (char **)&netmask);
  verify_netmask(&rt, (char *)netmask);

  if ((action == 1) && (rt.rt_flags & RTF_HOST))
    (((struct sockaddr_in *)&((rt).rt_genmask))->sin_addr.s_addr) = INVALID_ADDR;

  sokfd = xsocket(AF_INET, SOCK_DGRAM, 0);
  if (action == 1) xioctl(sokfd, SIOCADDRT, &rt);
  else xioctl(sokfd, SIOCDELRT, &rt);
  xclose(sokfd);
}

/*
 * get prefix len (if any) and remove the prefix from target ip.
 * if no prefix then set default prefix len.
 */
static void is_prefix_inet6(char **tip, struct in6_rtmsg *rt)
{
  unsigned long plen;
  char *prefix = strchr(*tip, '/');

  if (prefix) {
    *prefix = '\0';
    plen = atolx_range(prefix + 1, 0, 128); //DEFAULT_PREFIXLEN);
  } else plen = DEFAULT_PREFIXLEN;

  rt->rtmsg_flags = (plen == DEFAULT_PREFIXLEN) ? (RTF_UP | RTF_HOST) : RTF_UP;
  rt->rtmsg_dst_len = plen;
}

/*
 * used to get the params like: metric, gw, dev and their values.
 * additionally set the flag values for mod and dyn.
 */
static void get_next_params_inet6(char **argv, struct sockaddr_in6 *sock_in6, struct in6_rtmsg *rt, char **dev_name)
{
  for (;*argv;argv++) {
    if (!strcmp(*argv, "mod")) rt->rtmsg_flags |= RTF_MODIFIED;
    else if (!strcmp(*argv, "dyn")) rt->rtmsg_flags |= RTF_DYNAMIC;
    else {
      if (!argv[1]) help_exit(0);

      if (!strcmp(*argv, "metric")) 
        rt->rtmsg_metric = atolx_range(*argv, 0, ULONG_MAX);
      else if (!strcmp(*argv, "gw")) {
        //route packets via a gateway.
        if (!(rt->rtmsg_flags & RTF_GATEWAY)) {
          if (!get_addrinfo(*argv, (struct sockaddr_in6 *) &sock_in6)) {
            memcpy(&rt->rtmsg_gateway, sock_in6->sin6_addr.s6_addr, sizeof(struct in6_addr));
            rt->rtmsg_flags |= RTF_GATEWAY;
          } else perror_exit("resolving '%s'", *argv);
        } else help_exit(0);
      } else if (!strcmp(*argv, "dev")) {
        if (!*dev_name) *dev_name = *argv;
      } else help_exit(0);
    }
  }
}

// add/del a route.
static void setroute_inet6(char **argv)
{
  struct sockaddr_in6 sock_in6;
  struct in6_rtmsg rt;
  char *targetip, *dev_name = 0;
  int sockfd, action = get_action(&argv, arglist1);

  if (!action || !*argv) help_exit(0);
  memset(&sock_in6, 0, sizeof(struct sockaddr_in6));
  memset(&rt, 0, sizeof(struct in6_rtmsg));
  targetip = *argv++;
  if (!*argv) help_exit(0);

  if (!strcmp(targetip, "default")) {
    rt.rtmsg_flags = RTF_UP;
    rt.rtmsg_dst_len = 0;
  } else {
    is_prefix_inet6((char **)&targetip, &rt);
    if (get_addrinfo(targetip, (struct sockaddr_in6 *) &sock_in6))
      perror_exit("resolving '%s'", targetip);
  }
  rt.rtmsg_metric = 1; //default metric.
  memcpy(&rt.rtmsg_dst, sock_in6.sin6_addr.s6_addr, sizeof(struct in6_addr));
  get_next_params_inet6(argv, &sock_in6, &rt, (char **)&dev_name);

  sockfd = xsocket(AF_INET6, SOCK_DGRAM, 0);
  if (dev_name) {
    char ifre_buf[sizeof(struct ifreq)] = {0,};
    struct ifreq *ifre = (struct ifreq*)ifre_buf;
    xstrncpy(ifre->ifr_name, dev_name, IFNAMSIZ);
    xioctl(sockfd, SIOGIFINDEX, ifre);
    rt.rtmsg_ifindex = ifre->ifr_ifindex;
  }          
  if (action == 1) xioctl(sockfd, SIOCADDRT, &rt);
  else xioctl(sockfd, SIOCDELRT, &rt);
  xclose(sockfd);
}

/*
 * format the dest and src address in ipv6 format.
 * e.g. 2002:6b6d:26c8:d:ea03:9aff:fe65:9d62
 */
static void ipv6_addr_formating(char *ptr, char *addr)
{
  int i = 0;
  while (i <= IPV6_ADDR_LEN) {
    if (!*ptr) {
      if (i == IPV6_ADDR_LEN) {
        addr[IPV6_ADDR_LEN - 1] = 0; //NULL terminating the ':' seperated address.
        break;
      }
      error_exit("IPv6 ip format error");
    }
    addr[i++] = *ptr++;
    if (!((i+1) % 5)) addr[i++] = ':'; //put ':' after 4th bit
  }
}

static void display_routes6(void)
{
  char iface[16] = {0,}, ipv6_dest_addr[41]; 
  char ipv6_src_addr[41], flag_val[10], buf2[INET6_ADDRSTRLEN];
  int prefixlen, metric, use, refcount, flag, items = 0;
  unsigned char buf[sizeof(struct in6_addr)];

  FILE *fp = xfopen("/proc/net/ipv6_route", "r");

  xprintf("Kernel IPv6 routing table\n"
      "%-43s%-40s Flags Metric Ref    Use Iface\n", "Destination", "Next Hop");

  while ((items = fscanf(fp, "%32s%x%*s%*x%32s%x%x%x%x%15s\n", ipv6_dest_addr+8,
          &prefixlen, ipv6_src_addr+8, &metric, &use, &refcount, &flag, 
          iface)) == 8)
  {
    if (!(flag & RTF_UP)) continue; //skip down interfaces.

    //ipv6_dest_addr+8: as the values are filled from the 8th location of the array.
    ipv6_addr_formating(ipv6_dest_addr+8, ipv6_dest_addr);
    ipv6_addr_formating(ipv6_src_addr+8, ipv6_src_addr);

    get_flag_value(flag_val, flag);
    if (inet_pton(AF_INET6, ipv6_dest_addr, buf) <= 0) perror_exit("inet");
    if (inet_ntop(AF_INET6, buf, buf2, INET6_ADDRSTRLEN))
      sprintf(toybuf, "%s/%d", buf2, prefixlen);

    if (inet_pton(AF_INET6, ipv6_src_addr, buf) <= 0) perror_exit("inet");
    if (inet_ntop(AF_INET6, buf, buf2, INET6_ADDRSTRLEN))
      xprintf("%-43s %-39s %-5s %-6d %-4d %5d %-8s\n",
          toybuf, buf2, flag_val, metric, refcount, use, iface);
  }
  if ((items > 0) && feof(fp)) perror_exit("fscanf");

  fclose(fp);
}

void route_main(void)
{
  if (!TT.family) TT.family = "inet";
  if (!*toys.optargs) {
    if (!strcmp(TT.family, "inet")) display_routes();
    else if (!strcmp(TT.family, "inet6")) display_routes6();
    else help_exit(0);
  } else {
    if (!strcmp(TT.family, "inet6")) setroute_inet6(toys.optargs);
    else setroute(toys.optargs);
  }
}
