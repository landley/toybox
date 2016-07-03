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
  default y
  help
    usage: netstat [-pWrxwutneal]

    Display networking information. Default is netsat -tuwx

    -r  routing table
    -a  all sockets (not just connected)
    -l  listening server sockets
    -t  TCP sockets
    -u  UDP sockets
    -w  raw sockets
    -x  unix sockets
    -e  extended info
    -n  don't resolve names
    -W  wide display
    -p  PID/Program name of sockets
*/

#define FOR_netstat
#include "toys.h"
#include <net/route.h>

GLOBALS(
  struct num_cache *inodes;
  int wpad;
);

// convert address into text format.
static void addr2str(int af, void *addr, unsigned port, char *buf, int len,
  char *proto)
{
  int pos, count;
  struct servent *ser = 0;

  // Convert to numeric address
  if (!inet_ntop(af, addr, buf, 256)) {
    *buf = 0;

    return;
  }
  buf[len] = 0;
  pos = strlen(buf);

  // If there's no port number, it's a local :* binding, nothing to look up.
  if (!port) {
    if (len-pos<2) pos = len-2;
    strcpy(buf+pos, ":*");

    return;
  }

  if (!(toys.optflags & FLAG_n)) {
    struct addrinfo hints, *result, *rp;
    char cut[4];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;

    if (!getaddrinfo(buf, NULL, &hints, &result)) {
      socklen_t sock_len = (af == AF_INET) ? sizeof(struct sockaddr_in)
        : sizeof(struct sockaddr_in6);

      // We assume that a failing getnameinfo dosn't stomp "buf" here.
      for (rp = result; rp; rp = rp->ai_next)
        if (!getnameinfo(rp->ai_addr, sock_len, buf, 256, 0, 0, 0)) break;
      freeaddrinfo(result);
      buf[len] = 0;
      pos = strlen(buf);
    }

    // Doesn't understand proto "tcp6", so truncate
    memcpy(cut, proto, 3);
    cut[3] = 0;
    ser = getservbyport(htons(port), cut);
  }

  // Append :service
  count = snprintf(0, 0, ":%u", port);
  if (ser) {
    count = snprintf(0, 0, ":%s", ser->s_name);
    // sheer paranoia
    if (count>=len) {
      count = len-1;
      ser->s_name[count] = 0;
    }
  }
  if (len-pos<count) pos = len-count;
  if (ser) sprintf(buf+pos, ":%s", ser->s_name);
  else sprintf(buf+pos, ":%u", port);
}

// Display info for tcp/udp/raw
static void show_ip(char *fname)
{
  char *ss_state = "UNKNOWN", buf[12], *s, *label = strrchr(fname, '/')+1;
  char *state_label[] = {"", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1",
                         "FIN_WAIT2", "TIME_WAIT", "CLOSE", "CLOSE_WAIT",
                         "LAST_ACK", "LISTEN", "CLOSING", "UNKNOWN"};
  struct passwd *pw;
  FILE *fp = fopen(fname, "r");

  if (!fp) {
     perror_msg("'%s'", fname);
     return;
  }

  if(!fgets(toybuf, sizeof(toybuf), fp)) return; //skip header.

  while (fgets(toybuf, sizeof(toybuf), fp)) {
    char lip[256], rip[256];
    union {
      struct {unsigned u; unsigned char b[4];} i4;
      struct {struct {unsigned a, b, c, d;} u; unsigned char b[16];} i6;
    } laddr, raddr;
    unsigned lport, rport, state, txq, rxq, num, uid, nitems;
    unsigned long inode;

    // Try ipv6, then try ipv4
    nitems = sscanf(toybuf,
      " %d: %8x%8x%8x%8x:%x %8x%8x%8x%8x:%x %x %x:%x %*X:%*X %*X %d %*d %ld",
      &num, &laddr.i6.u.a, &laddr.i6.u.b, &laddr.i6.u.c,
      &laddr.i6.u.d, &lport, &raddr.i6.u.a, &raddr.i6.u.b,
      &raddr.i6.u.c, &raddr.i6.u.d, &rport, &state, &txq, &rxq,
      &uid, &inode);

    if (nitems!=16) {
      nitems = sscanf(toybuf,
        " %d: %x:%x %x:%x %x %x:%x %*X:%*X %*X %d %*d %ld",
        &num, &laddr.i4.u, &lport, &raddr.i4.u, &rport, &state, &txq,
        &rxq, &uid, &inode);

      if (nitems!=10) continue;
      nitems = AF_INET;
    } else nitems = AF_INET6;

    // Should we display this? (listening or all or TCP/UDP/RAW)
    if (!((toys.optflags & FLAG_l) && (!rport && (state & 0xA)))
      && !(toys.optflags & FLAG_a) && !(rport & (0x10 | 0x20 | 0x40)))
        continue;

    addr2str(nitems, &laddr, lport, lip, TT.wpad, label);
    addr2str(nitems, &raddr, rport, rip, TT.wpad, label);

    // Display data
    s = label;
    if (strstart(&s, "tcp")) {
      int sz = ARRAY_LEN(state_label);
      if (!state || state >= sz) state = sz-1;
      ss_state = state_label[state];
    } else if (strstart(&s, "udp")) {
      if (state == 1) ss_state = state_label[state];
      else if (state == 7) ss_state = "";
    } else if (strstart(&s, "raw")) sprintf(ss_state = buf, "%u", state);

    if (!(toys.optflags & FLAG_n) && (pw = bufgetpwuid(uid)))
      snprintf(toybuf, sizeof(toybuf), "%s", pw->pw_name);
    else snprintf(toybuf, sizeof(toybuf), "%d", uid);

    printf("%-6s%6d%7d ", label, rxq, txq);
    printf("%*.*s %*.*s ", -TT.wpad, TT.wpad, lip, -TT.wpad, TT.wpad, rip);
    printf("%-11s", ss_state);
    if ((toys.optflags & FLAG_e)) printf(" %-10s %-11ld", toybuf, inode);
    if ((toys.optflags & FLAG_p)) {
      struct num_cache *nc = get_num_cache(TT.inodes, inode);

      printf(" %s", nc ? nc->data : "-");
    }
    xputc('\n');
  }
  fclose(fp);
}

static void show_unix_sockets(void)
{
  char *types[] = {"","STREAM","DGRAM","RAW","RDM","SEQPACKET","DCCP","PACKET"},
       *states[] = {"","LISTENING","CONNECTING","CONNECTED","DISCONNECTING"},
       *s, *ss;
  unsigned long refcount, flags, type, state, inode;
  FILE *fp = xfopen("/proc/net/unix", "r");

  if(!fgets(toybuf, sizeof(toybuf), fp)) return; //skip header.

  while (fgets(toybuf, sizeof(toybuf), fp)) {
    unsigned offset = 0;

    // count = 6 or 7 (first field ignored, sockets don't always have filenames)
    if (6<sscanf(toybuf, "%*p: %lX %*X %lX %lX %lX %lu %n",
      &refcount, &flags, &type, &state, &inode, &offset))
        continue;

    // Linux exports only SO_ACCEPTCON since 2.3.15pre3 in 1999, but let's
    // filter in case they add more someday.
    flags &= 1<<16;

    // Only show unconnected listening sockets with -a
    if (state==1 && flags && !(toys.optflags&FLAG_a)) continue;

    if (type==10) type = 7; // move SOCK_PACKET into line
    if (type>ARRAY_LEN(types)) type = 0;
    if (state>ARRAY_LEN(states) || (state==1 && !flags)) state = 0;
    sprintf(toybuf, "[ %s]", flags ? "ACC " : "");

    printf("unix  %-6ld %-11s %-10s %-13s %8lu ",
      refcount, toybuf, types[type], states[state], inode);
    if (toys.optflags & FLAG_p) {
      struct num_cache *nc = get_num_cache(TT.inodes, inode);

      printf("%-19.19s", nc ? nc->data : "-");
    }

    if (offset) {
      if ((ss = strrchr(s = toybuf+offset, '\n'))) *ss = 0;
      printf("%s", s);
    }
    xputc('\n');
  }

  fclose(fp);
}

static int scan_pids(struct dirtree *node)
{
  char *s = toybuf+256;
  struct dirent *entry;
  DIR *dp;
  int pid, dirfd;

  if (!node->parent) return DIRTREE_RECURSE;
  if (!(pid = atol(node->name))) return 0;

  sprintf(toybuf, "/proc/%d/cmdline", pid);
  if (!(readfile(toybuf, toybuf, 256))) return 0;

  sprintf(s, "%d/fd", pid);
  if (-1==(dirfd = openat(dirtree_parentfd(node), s, O_RDONLY))) return 0;
  if (!(dp = fdopendir(dirfd))) {
    close(dirfd);

    return 0;
  }

  while ((entry = readdir(dp))) {
    s = toybuf+256;
    if (!readlinkat0(dirfd, entry->d_name, s, sizeof(toybuf)-256)) continue;
    // Can the "[0000]:" happen in a modern kernel?
    if (strstart(&s, "socket:[") || strstart(&s, "[0000]:")) {
      long long ll = atoll(s);

      sprintf(s, "%d/%s", pid, getbasename(toybuf));
      add_num_cache(&TT.inodes, ll, s, strlen(s)+1);
    }
  }
  closedir(dp);

  return 0;
}

/*
 * extract inet4 route info from /proc/net/route file and display it.
 */
static void display_routes(void)
{
  static const char flagchars[] = "GHRDMDAC";
  static const unsigned flagarray[] = {
    RTF_GATEWAY, RTF_HOST, RTF_REINSTATE, RTF_DYNAMIC, RTF_MODIFIED
  };
  unsigned long dest, gate, mask;
  int flags, ref, use, metric, mss, win, irtt;
  char *out = toybuf, *flag_val;
  char iface[64]={0};
  FILE *fp = xfopen("/proc/net/route", "r");

  if(!fgets(toybuf, sizeof(toybuf), fp)) return; //skip header.

  printf("Kernel IP routing table\n"
          "Destination\tGateway \tGenmask \tFlags %s Iface\n",
          !(toys.optflags&FLAG_e) ? "  MSS Window  irtt" : "Metric Ref    Use");

  while (fgets(toybuf, sizeof(toybuf), fp)) {
     char *destip = 0, *gateip = 0, *maskip = 0;

     if (11 != sscanf(toybuf, "%63s%lx%lx%X%d%d%d%lx%d%d%d", iface, &dest,
       &gate, &flags, &ref, &use, &metric, &mask, &mss, &win, &irtt))
         break;

    // skip down interfaces.
    if (!(flags & RTF_UP)) continue;

// TODO /proc/net/ipv6_route

    if (dest) {
      if (inet_ntop(AF_INET, &dest, out, 16)) destip = out;
    } else destip = (toys.optflags&FLAG_n) ? "0.0.0.0" : "default";
    out += 16;

    if (gate) {
      if (inet_ntop(AF_INET, &gate, out, 16)) gateip = out;
    } else gateip = (toys.optflags&FLAG_n) ? "0.0.0.0" : "*";
    out += 16;

// TODO /24
    //For Mask
    if (inet_ntop(AF_INET, &mask, out, 16)) maskip = out;
    else maskip = "?";
    out += 16;

    //Get flag Values
    flag_val = out;
    *out++ = 'U';
    for (dest = 0; dest < ARRAY_LEN(flagarray); dest++)
      if (flags&flagarray[dest]) *out++ = flagchars[dest];
    *out = 0;
    if (flags & RTF_REJECT) *flag_val = '!';

    printf("%-15.15s %-15.15s %-16s%-6s", destip, gateip, maskip, flag_val);
    if (!(toys.optflags & FLAG_e))
      printf("%5d %-5d %6d %s\n", mss, win, irtt, iface);
    else printf("%-6d %-2d %7d %s\n", metric, ref, use, iface);
  }

  fclose(fp);
}

void netstat_main(void)
{
  int tuwx = FLAG_t|FLAG_u|FLAG_w|FLAG_x;
  char *type = "w/o";

  TT.wpad = (toys.optflags&FLAG_W) ? 51 : 23;
  if (!(toys.optflags&(FLAG_r|tuwx))) toys.optflags |= tuwx;
  if (toys.optflags & FLAG_r) display_routes();
  if (!(toys.optflags&tuwx)) return;

  if (toys.optflags & FLAG_a) type = "established and";
  else if (toys.optflags & FLAG_l) type = "only";

  if (toys.optflags & FLAG_p) dirtree_read("/proc", scan_pids);

  if (toys.optflags&(FLAG_t|FLAG_u|FLAG_w)) {
    printf("Active %s (%s servers)\n", "Internet connections", type);
    printf("Proto Recv-Q Send-Q %*s %*s State      ", -TT.wpad, "Local Address",
      -TT.wpad, "Foreign Address");
    if (toys.optflags & FLAG_e) printf(" User       Inode      ");
    if (toys.optflags & FLAG_p) printf(" PID/Program Name");
    xputc('\n');

    if (toys.optflags & FLAG_t) {
      show_ip("/proc/net/tcp");
      show_ip("/proc/net/tcp6");
    }
    if (toys.optflags & FLAG_u) {
      show_ip("/proc/net/udp");
      show_ip("/proc/net/udp6");
    }
    if (toys.optflags & FLAG_w) {
      show_ip("/proc/net/raw");
      show_ip("/proc/net/raw6");
    }
  }

  if (toys.optflags & FLAG_x) {
    printf("Active %s (%s servers)\n", "UNIX domain sockets", type);

    printf("Proto RefCnt Flags\t Type\t    State\t    %s Path\n",
      (toys.optflags&FLAG_p) ? "PID/Program Name" : "I-Node");
    show_unix_sockets();
  }

  if ((toys.optflags & FLAG_p) && CFG_TOYBOX_FREE)
    llist_traverse(TT.inodes, free);
  toys.exitval = 0;
}
