/* netstat.c - Display Linux networking subsystem.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * Not in SUSv4.
 *
USE_NETSTAT(NEWTOY(netstat, "pWrxwutneal", TOYFLAG_BIN))
config NETSTAT
  bool "netstat"
  default n
  help
    usage: netstat [-pWrxwutneal]

    Display networking information.

    -r  Display routing table.
    -a  Display all sockets (Default: Connected).
    -l  Display listening server sockets.
    -t  Display TCP sockets.
    -u  Display UDP sockets.
    -w  Display Raw sockets.
    -x  Display Unix sockets.
    -e  Display other/more information.
    -n  Don't resolve names.
    -W  Wide Display.
    -p  Display PID/Program name for sockets.
*/

#define FOR_netstat
#include "toys.h"
#include <net/route.h>

typedef union _iaddr {
  unsigned u;
  unsigned char b[4];
} iaddr;

typedef union _iaddr6 {
  struct {
    unsigned a;
    unsigned b;
    unsigned c;
    unsigned d;
  } u;
  unsigned char b[16];
} iaddr6;

#define ADDR_LEN (INET6_ADDRSTRLEN + 1 + 5 + 1) //IPv6 addr len + : + port + '\0'

//For unix states
enum {
	SOCK_ACCEPTCON = (1 << 16),  //performed a listen.
	SOCK_WAIT_DATA = (1 << 17),  //wait data to read.
	SOCK_NO_SPACE = (1 << 18),  //no space to write.
};

#define SOCK_NOT_CONNECTED 1
//For PID/Progrma Name
#define PROGRAM_NAME "PID/Program Name"
#define PROGNAME_LEN 21

typedef struct _pidlist {
  struct _pidlist *next;
  long inode;
  char name[PROGNAME_LEN];
} PID_LIST;
PID_LIST *pid_list = NULL;

/*
 * Get base name from the input name.
 */
static const char *get_basename(char *name)
{
  const char *c = strrchr(name, '/');
  if (c) return c + 1;
  return name;
}

/*
 * locate character in string.
 */
static char *strchr_nul(char *s, int c)
{
  while(*s != '\0' && *s != c) s++;
  return (char*)s;
}

// Find out if the last character of a string matches with the given one.
// Don't underrun the buffer if the string length is 0.
static char *find_last_char(char *str, int c)
{
  if (str && *str) {
    size_t sz = strlen(str) - 1;
    str += sz;
    if ( (unsigned char)*str == c) return (char*)str;
  }
  return NULL;
}
/*
 * Concat path and the file name.
 */
static char *append_pathandfile(char *path, char *fname)
{
  char *c;
  if (!path) path = "";
  c = find_last_char(path, '/');
  while (*fname == '/') fname++;
  return xmprintf("%s%s%s", path, (c)? "" : "/", fname);
}
/*
 * Concat sub-path and the file name.
 */
static char *append_subpathandfile(char *path, char *fname)
{
#define ISDOTORDOTDOT(s) ((s)[0] == '.' && (!(s)[1] || ((s)[1] == '.' && !(s)[2])))
  if(!fname) return NULL;
  if(ISDOTORDOTDOT(fname)) return NULL;
  return append_pathandfile(path, fname);
#undef ISDOTORDOTDOT
}
/*
 * used to converts string into int and validate the input str for invalid int value or out-of-range.
 */
static unsigned get_strtou(char *str, char **endp, int base)
{
  unsigned long uli;
  char *endptr;

  if (!isalnum(str[0])) {
    errno = ERANGE;
    return UINT_MAX;
  }
  errno = 0;
  uli = strtoul(str, &endptr, base);
  if (uli > UINT_MAX) {
    errno = ERANGE;
    return UINT_MAX;
  }

  if (endp) *endp = endptr;
  if (endptr[0]) {
    if (isalnum(endptr[0]) || errno) { //"123abc" or out-of-range
      errno = ERANGE;
      return UINT_MAX;
    }
    errno = EINVAL;
  }
  return uli;
}
/*
 * used to retrive pid name from pid list.
 */
static const char *get_pid_name(unsigned long inode)
{
  PID_LIST *tmp;
  for (tmp = pid_list; tmp; tmp = tmp->next)
    if (tmp->inode == inode) return tmp->name;
  return "-";
}
/*
 * For TCP/UDP/RAW display data.
 */
static void display_data(unsigned rport, char *label, unsigned rxq, unsigned txq, char *lip, char *rip, unsigned state, unsigned long inode)
{
  char *ss_state = "UNKNOWN", buf[12];
  char *state_label[] = {"", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
  		                 "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING", "UNKNOWN"};
  if (!strcmp(label, "tcp")) {
    int sz = ARRAY_LEN(state_label);
    if (!state || state >= sz) state = sz-1;
    ss_state = state_label[state];
  }
  else if (!strcmp(label, "udp")) {
    if (state == 1) ss_state = state_label[state];
    else if (state == 7) ss_state = "";
  }
  else if (!strcmp(label, "raw")) sprintf(ss_state = buf, "%u", state);

  if ( (toys.optflags & FLAG_W) && (toys.optflags & FLAG_p))
    xprintf("%3s   %6d %6d %-51s %-51s %-12s%s\n", label, rxq, txq, lip, rip, ss_state, get_pid_name(inode));
  else if (toys.optflags & FLAG_W)
    xprintf("%3s   %6d %6d %-51s %-51s %-12s\n", label, rxq, txq, lip, rip, ss_state);
  else if (toys.optflags & FLAG_p)
    xprintf("%3s   %6d %6d %-23s %-23s %-12s%s\n", label, rxq, txq, lip, rip, ss_state, get_pid_name(inode));
  else xprintf("%3s   %6d %6d %-23s %-23s %-12s\n", label, rxq, txq, lip, rip, ss_state);
}
/*
 * For TCP/UDP/RAW show data.
 */
static void show_data(unsigned rport, char *label, unsigned rxq, unsigned txq, char *lip, char *rip, unsigned state, unsigned long inode)
{
  if (toys.optflags & FLAG_l) {
    if (!rport && (state && 0xA)) display_data(rport, label, rxq, txq, lip, rip, state, inode);
  } else if (toys.optflags & FLAG_a) display_data(rport, label, rxq, txq, lip, rip, state, inode);
  //rport && (TCP | UDP | RAW)
  else if (rport && (0x10 | 0x20 | 0x40)) display_data(rport, label, rxq, txq, lip, rip, state, inode);
}
/*
 * used to get service name.
 */
static char *get_servname(int port, char *label)
{
  int lport = htons(port);
  if (!lport) return xmprintf("%s", "*");
  struct servent *ser = getservbyport(lport, label);
  if (ser) return xmprintf("%s", ser->s_name);
  return xmprintf("%u", (unsigned)ntohs(lport));
}
/*
 * used to convert address into text format.
 */
static void addr2str(int af, void *addr, unsigned port, char *buf, char *label)
{
  char ip[ADDR_LEN] = {0,};
  if (!inet_ntop(af, addr, ip, ADDR_LEN)) {
    *buf = '\0';
    return;
  }
  size_t iplen = strlen(ip);
  if (!port) {
    strncat(ip+iplen, ":*", ADDR_LEN-iplen-1);
    memcpy(buf, ip, ADDR_LEN);
    return;
  }

  if (!(toys.optflags & FLAG_n)) {
    struct addrinfo hints, *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;

    if (!getaddrinfo(ip, NULL, &hints, &result)) {
      char hbuf[NI_MAXHOST] = {0,}, sbuf[NI_MAXSERV] = {0,};
      socklen_t sock_len;
      char *sname = NULL;
      int plen = 0;

      if (af == AF_INET) sock_len = sizeof(struct sockaddr_in);
      else sock_len = sizeof(struct sockaddr_in6);

      for (rp = result; rp; rp = rp->ai_next)
        if (!getnameinfo(rp->ai_addr, sock_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICSERV))
          break;

      freeaddrinfo(result);
      sname = get_servname(port, label);
      plen = strlen(sname);
      if (*hbuf) {
        memset(ip, 0, ADDR_LEN);
        memcpy(ip, hbuf, (ADDR_LEN - plen - 2));
        iplen = strlen(ip);
      }
      snprintf(ip + iplen, ADDR_LEN-iplen, ":%s", sname);
      free(sname);
    }
  }
  else snprintf(ip+iplen, ADDR_LEN-iplen, ":%d", port);
  memcpy(buf, ip, ADDR_LEN);
}
/*
 * display ipv4 info for TCP/UDP/RAW.
 */
static void show_ipv4(char *fname, char *label)
{
  FILE *fp = fopen((char *)fname, "r");
  if (!fp) {
     perror_msg("'%s'", fname);
     return;
  }
  fgets(toybuf, sizeof(toybuf), fp); //skip header.
  while (fgets(toybuf, sizeof(toybuf), fp)) {
    char lip[ADDR_LEN] = {0,}, rip[ADDR_LEN] = {0,};
    iaddr laddr, raddr;
    unsigned lport, rport, state, txq, rxq, num, uid;
    unsigned long inode;

    int nitems = sscanf(toybuf, " %d: %x:%x %x:%x %x %x:%x %*X:%*X %*X %d %*d %ld",
        &num, &laddr.u, &lport, &raddr.u, &rport, &state, &txq, &rxq, &uid, &inode);
    if (nitems == 10) {
      addr2str(AF_INET, &laddr, lport, lip, label);
      addr2str(AF_INET, &raddr, rport, rip, label);
      show_data(rport, label, rxq, txq, lip, rip, state, inode);
    }
  }//End of While
  fclose(fp);
}
/*
 * display ipv6 info for TCP/UDP/RAW.
 */
static void show_ipv6(char *fname, char *label)
{
  FILE *fp = fopen((char *)fname, "r");
  if (!fp) {
    perror_msg("'%s'", fname);
    return;
  }
  fgets(toybuf, sizeof(toybuf), fp); //skip header.
  while (fgets(toybuf, sizeof(toybuf), fp)) {
    char lip[ADDR_LEN] = {0,}, rip[ADDR_LEN] = {0,};
    iaddr6 laddr6, raddr6;
    unsigned lport, rport, state, txq, rxq, num, uid;
    unsigned long inode;
    int nitems = sscanf(toybuf, " %d: %8x%8x%8x%8x:%x %8x%8x%8x%8x:%x %x %x:%x %*X:%*X %*X %d %*d %ld",
        &num, &laddr6.u.a, &laddr6.u.b, &laddr6.u.c, &laddr6.u.d, &lport, &raddr6.u.a, &raddr6.u.b,
        &raddr6.u.c, &raddr6.u.d, &rport, &state, &txq, &rxq, &uid, &inode);
    if (nitems == 16) {
      addr2str(AF_INET6, &laddr6, lport, lip, label);
      addr2str(AF_INET6, &raddr6, rport, rip, label);
      show_data(rport, label, rxq, txq, lip, rip, state, inode);
    }
  }//End of While
  fclose(fp);
}
/*
 * display unix socket info.
 */
static void show_unix_sockets(char *fname, char *label)
{
  FILE *fp = fopen((char *)fname, "r");
  if (!fp) {
    perror_msg("'%s'", fname);
    return;
  }
  fgets(toybuf, sizeof(toybuf), fp); //skip header.
  while (fgets(toybuf, sizeof(toybuf), fp)) {
    unsigned long int refcount, label, flags, inode;
    int nitems = 0, path_offset = 0, type, state;
    char sock_flags[32] = {0,}, *sock_type, *sock_state, *bptr = toybuf;

    if (!toybuf[0]) continue;
    nitems = sscanf(toybuf, "%*p: %lX %lX %lX %X %X %lu %n",
        &refcount, &label, &flags, &type, &state, &inode, &path_offset);
    //for state one less
    if (nitems < 6) break;
    if (toys.optflags & FLAG_l) {
      if ( !((state == SOCK_NOT_CONNECTED) && (flags & SOCK_ACCEPTCON)) ) continue;
    } else if (!(toys.optflags & FLAG_a)) {
      if ((state == SOCK_NOT_CONNECTED) && (flags & SOCK_ACCEPTCON)) continue;
    }

    //prepare socket type, state and flags.
    {
      char *ss_type[] = { "", "STREAM", "DGRAM", "RAW", "RDM", "SEQPACKET", "UNKNOWN"};
      char *ss_state[] = { "FREE", "LISTENING", "CONNECTING", "CONNECTED", "DISCONNECTING", "UNKNOWN"};

      int sz = ARRAY_LEN(ss_type);//sizeof(ss_type)/sizeof(ss_type[0]);
      if ( (type < SOCK_STREAM) || (type > SOCK_SEQPACKET) ) sock_type = ss_type[sz-1];
      else sock_type = ss_type[type];

      sz = ARRAY_LEN(ss_state);//sizeof(ss_state)/sizeof(ss_state[0]);
      if ((state < 0) || (state > sz-2)) sock_state = ss_state[sz-1];
      else if (state == SOCK_NOT_CONNECTED) {
        if (flags & SOCK_ACCEPTCON) sock_state = ss_state[state];
        else sock_state = " ";
      } else sock_state = ss_state[state];

      strcpy(sock_flags, "[ ");
      if (flags & SOCK_ACCEPTCON) strcat(sock_flags, "ACC ");
      if (flags & SOCK_WAIT_DATA) strcat(sock_flags, "W ");
      if (flags & SOCK_NO_SPACE) strcat(sock_flags, "N ");
      strcat(sock_flags, "]");
    }
    xprintf("%-5s %-6ld %-11s %-10s %-13s %6lu ", (!label ? "unix" : "??"), refcount, sock_flags, sock_type, sock_state, inode);
    if (toys.optflags & FLAG_p) xprintf("%-20s", get_pid_name(inode));

    bptr += path_offset;
    *strchr_nul(bptr, '\n') = '\0';
    xprintf("%s\n", bptr);
  }//End of while
  fclose(fp);
}
/*
 * extract inode value from the link.
 */
static long ss_inode(char *link)
{
  long inode = -1;
  //"link = socket:[12345]", get "12345" as inode.
  if (!strncmp(link, "socket:[", sizeof("socket:[")-1)) {
    inode = get_strtou(link + sizeof("socket:[")-1, (char**)&link, 0);
    if (*link != ']') inode = -1;
  }
  //"link = [0000]:12345", get "12345" as inode.
  else if (!strncmp(link, "[0000]:", sizeof("[0000]:")-1)) {
    inode = get_strtou(link + sizeof("[0000]:")-1, NULL, 0);
    //if not NULL terminated.
    if (errno) inode = -1;
  }
  return inode;
}
/*
 * add inode and progname in the pid list.
 */
static void add2list(long inode, char *progname)
{
  PID_LIST *node = pid_list;
  for(; node; node = node->next) {
    if(node->inode == inode)
      return;
  }
  PID_LIST *new = (PID_LIST *)xzalloc(sizeof(PID_LIST));
  new->inode = inode;
  xstrncpy(new->name, progname, PROGNAME_LEN);
  new->next = pid_list;
  pid_list = new;
}
/*
 * add pid info in the list.
 */
static void extract_inode(char *path, char *progname)
{
  DIR *dp;
  struct dirent *entry;

  if (!(dp = opendir(path))) {
    if (errno == EACCES) return;
    else perror_exit("%s", path);
  }
  while ((entry = readdir(dp))) {
    char *link = NULL, *fname = append_subpathandfile(path, entry->d_name);
    if (!fname) continue;
    link = xreadlink(fname);
    if (link) {
      long inode = ss_inode(link);
      free(link);
      if (inode != -1) add2list(inode, progname);
    }
    free(fname);
  }//end of while.
  closedir(dp);
}
/*
 * prepare the list for all pids in /proc directory.
 */
static void get_pid_list(void)
{
  DIR *dp;
  struct dirent *entry;
  char path[64] = {0,};
  uid_t uid = geteuid();

  if (!(dp = opendir("/proc"))) perror_exit("opendir");

  while ((entry = readdir(dp))) {
    int fd, nitems = 0, length = 0;
    char *pid, *progname;

    if (!isdigit(*entry->d_name)) continue;
    pid = entry->d_name;
    length = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
    if (sizeof(path) <= length) continue;

    fd = xopen(path, O_RDONLY);
    nitems = readall(fd, toybuf, sizeof(toybuf) - 1);
    xclose(fd);
    if (nitems < 1) continue;
    toybuf[nitems] = '\0';
    strcpy(path + length - (sizeof("cmdline")-1), "fd");
    progname = append_pathandfile(pid, (char *)get_basename(toybuf)); //e.g. progname = 2054/gnome-keyring-daemon
    extract_inode(path, progname);
    free(progname);
  }//end of while.
  closedir(dp);

  if (uid) fprintf(stderr, "(Not all processes could be identified, non-owned process info "
      "will not be shown, you would have to be root to see it all.)\n");
}
/*
 * Dealloc pid list.
 */
static void clean_pid_list(void)
{
  PID_LIST *tmp;
  while (pid_list) {
    tmp = pid_list->next;
    free(pid_list);
    pid_list = tmp;
  }
}
/*
 * For TCP/UDP/RAW show the header.
 */
static void show_header(void)
{
  if ((toys.optflags & FLAG_W) && (toys.optflags & FLAG_p))
    xprintf("\nProto Recv-Q Send-Q %-51s %-51s %-12s%s\n", "Local Address", "Foreign Address", "State", PROGRAM_NAME);
  else if (toys.optflags & FLAG_p)
    xprintf("\nProto Recv-Q Send-Q %-23s %-23s %-12s%s\n", "Local Address", "Foreign Address", "State", PROGRAM_NAME);
  else if (toys.optflags & FLAG_W)
	  xprintf("\nProto Recv-Q Send-Q %-51s %-51s State     \n", "Local Address", "Foreign Address");
  else xprintf("\nProto Recv-Q Send-Q %-23s %-23s State     \n", "Local Address", "Foreign Address");
}
/*
 * used to get the flag values for route command.
 */
static void get_flag_value(char **flagstr, int flags)
{
  int i = 0;
  char *str = *flagstr;
  static const char flagchars[] = "GHRDMDAC";
  static const unsigned flagarray[] = {
    RTF_GATEWAY,
    RTF_HOST,
    RTF_REINSTATE,
    RTF_DYNAMIC,
    RTF_MODIFIED,
    RTF_DEFAULT,
    RTF_ADDRCONF,
    RTF_CACHE
  };
  *str++ = 'U';
  while ( (*str = flagchars[i]) ) {
    if (flags & flagarray[i++]) ++str;
  }
}
/*
 * extract inet4 route info from /proc/net/route file and display it.
 */
static void display_routes(int is_more_info, int notresolve)
{
#define IPV4_MASK (RTF_GATEWAY|RTF_HOST|RTF_REINSTATE|RTF_DYNAMIC|RTF_MODIFIED)
  unsigned long dest, gate, mask;
  int flags, ref, use, metric, mss, win, irtt;
  char iface[64]={0,};
  char *flag_val = xzalloc(10); //there are 9 flags "UGHRDMDAC" for route.

  FILE *fp = xfopen("/proc/net/route", "r");
  xprintf("Kernel IP routing table\n"
                   "Destination     Gateway         Genmask         Flags %s Iface\n",
  	                        is_more_info ? "  MSS Window  irtt" : "Metric Ref    Use");
  fgets(toybuf, sizeof(toybuf), fp); //skip 1st line.
  while (fgets(toybuf, sizeof(toybuf), fp)) {
     int nitems = 0;
     char *destip = NULL, *gateip = NULL, *maskip = NULL;
     memset(flag_val, 0, 10);

     nitems = sscanf(toybuf, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n",
                 iface, &dest, &gate, &flags, &ref, &use, &metric, &mask, &mss, &win, &irtt);
     if (nitems != 11) {//EOF with no (nonspace) chars read.
       if ((nitems < 0) && feof(fp)) break;
      perror_exit("sscanf");
    }
    //skip down interfaces.
    if (!(flags & RTF_UP)) continue;

    if (dest) {//For Destination
      if (inet_ntop(AF_INET, &dest, toybuf, sizeof(toybuf)) ) destip = xstrdup(toybuf);
    } else {
      if (!notresolve) destip = xstrdup("default");
      else destip = xstrdup("0.0.0.0");
    }
    if (gate) {//For Gateway
      if (inet_ntop(AF_INET, &gate, toybuf, sizeof(toybuf)) ) gateip = xstrdup(toybuf);
    } else {
      if (!notresolve) gateip = xstrdup("*");
      else gateip = xstrdup("0.0.0.0");
    }
    //For Mask
    if (inet_ntop(AF_INET, &mask, toybuf, sizeof(toybuf)) ) maskip = xstrdup(toybuf);

    //Get flag Values
    get_flag_value(&flag_val, (flags & IPV4_MASK));
    if (flags & RTF_REJECT) flag_val[0] = '!';
    xprintf("%-15.15s %-15.15s %-16s%-6s", destip, gateip, maskip, flag_val);
    if (destip) free(destip);
    if (gateip) free(gateip);
    if (maskip) free(maskip);
    if (is_more_info) xprintf("%5d %-5d %6d %s\n", mss, win, irtt, iface);
    else xprintf("%-6d %-2d %7d %s\n", metric, ref, use, iface);
  }//end of while.
  fclose(fp);
  if (flag_val) free(flag_val);
#undef IPV4_MASK
  return;
}
/*
 * netstat utily main function.
 */
void netstat_main(void)
{
#define IS_NETSTAT_PROTO_FLAGS_UP (toys.optflags & (FLAG_t | FLAG_u | FLAG_w | FLAG_x))
  int flag_listen_and_all = 0;
  if (!toys.optflags) toys.optflags = FLAG_t | FLAG_u | FLAG_w | FLAG_x;

  //When a is set
  if (toys.optflags & FLAG_a) flag_listen_and_all = 1;
  //when a and l both are set
  if ( (toys.optflags & FLAG_a) && (toys.optflags & FLAG_l) )
    toys.optflags &= ~FLAG_l;
  //when only a is set
  if ( (toys.optflags & FLAG_a) && (!IS_NETSTAT_PROTO_FLAGS_UP) )
    toys.optflags |= FLAG_t | FLAG_u | FLAG_w | FLAG_x;
  //when only l is set
  if ( (toys.optflags & FLAG_l) && (!IS_NETSTAT_PROTO_FLAGS_UP) )
    toys.optflags |= FLAG_t | FLAG_u | FLAG_w | FLAG_x;
  //when only e/n is set
  if( ((toys.optflags & FLAG_e) || (toys.optflags & FLAG_n)) && (!IS_NETSTAT_PROTO_FLAGS_UP) )
	  toys.optflags |= FLAG_t | FLAG_u | FLAG_w | FLAG_x;
  //when W is set
  if ( (toys.optflags & FLAG_W) && (!IS_NETSTAT_PROTO_FLAGS_UP) )
    toys.optflags |= FLAG_t | FLAG_u | FLAG_w | FLAG_x;
   //when p is set
  if ( (toys.optflags & FLAG_p) && (!IS_NETSTAT_PROTO_FLAGS_UP) )
    toys.optflags |= FLAG_t | FLAG_u | FLAG_w | FLAG_x;

  //Display routing table.
  if (toys.optflags & FLAG_r) {
    display_routes(!(toys.optflags & FLAG_e), (toys.optflags & FLAG_n));
    return;
  }

  if (toys.optflags & FLAG_p) get_pid_list();

  //For TCP/UDP/RAW.
  if ( (toys.optflags & FLAG_t) || (toys.optflags & FLAG_u) || (toys.optflags & FLAG_w) ) {
    xprintf("Active Internet connections ");

    if (flag_listen_and_all) xprintf("(servers and established)");
    else if (toys.optflags & FLAG_l) xprintf("(only servers)");
    else xprintf("(w/o servers)");

    show_header();
    if (toys.optflags & FLAG_t) {//For TCP
      show_ipv4("/proc/net/tcp",  "tcp");
      show_ipv6("/proc/net/tcp6", "tcp");
    }
    if (toys.optflags & FLAG_u) {//For UDP
      show_ipv4("/proc/net/udp",  "udp");
      show_ipv6("/proc/net/udp6", "udp");
    }
    if (toys.optflags & FLAG_w) {//For raw
      show_ipv4("/proc/net/raw",  "raw");
      show_ipv6("/proc/net/raw6", "raw");
    }
  }
  if (toys.optflags & FLAG_x) {//For UNIX.
    xprintf("Active UNIX domain sockets ");
    if (flag_listen_and_all) xprintf("(servers and established)");
    else if (toys.optflags & FLAG_l) xprintf("(only servers)");
    else xprintf("(w/o servers)");

    if (toys.optflags & FLAG_p) xprintf("\nProto RefCnt Flags       Type       State         I-Node %s    Path\n", PROGRAM_NAME);
    else xprintf("\nProto RefCnt Flags       Type       State         I-Node Path\n");
    show_unix_sockets("/proc/net/unix", "unix");
  }
  if (toys.optflags & FLAG_p) clean_pid_list();
  if (toys.exitval) toys.exitval = 0;
#undef IS_NETSTAT_PROTO_FLAGS_UP
}
