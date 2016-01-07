/* lsof.c - list open files.
 *
 * Copyright 2015 The Android Open Source Project

USE_LSOF(NEWTOY(lsof, "lp:t", TOYFLAG_USR|TOYFLAG_BIN))

config LSOF
  bool "lsof"
  default n
  help
    usage: lsof [-lt] [-p PID1,PID2,...] [NAME]...

    Lists open files. If names are given on the command line, only
    those files will be shown.

    -l	list uids numerically
    -p	for given comma-separated pids only (default all pids)
    -t	terse (pid only) output
*/

#define FOR_lsof
#include "toys.h"

GLOBALS(
  char *pids;

  struct stat *sought_files;

  struct double_list *files;
  int last_shown_pid;
  int shown_header;
)

struct proc_info {
  char cmd[10];
  int pid;
  char user[12];
};

struct file_info {
  char *next, *prev;

  // For output.
  struct proc_info pi;
  char fd[8];
  char rw;
  char locks;
  char type[10];
  char device[32];
  char size_off[32];
  char node[32];
  char* name;

  // For filtering.
  dev_t st_dev;
  ino_t st_ino;
};

static int filter_matches(struct file_info *fi)
{
  struct stat *sb = TT.sought_files;

  for (; sb != &(TT.sought_files[toys.optc]); ++sb) {
    if (sb->st_dev == fi->st_dev && sb->st_ino == fi->st_ino) return 1;
  }
  return 0;
}

static void print_header()
{
  // TODO: llist_traverse to measure the columns first.
  char* names[] = {
    "COMMAND", "PID", "USER", "FD", "TYPE", "DEVICE", "SIZE/OFF", "NODE", "NAME"
  };
  printf("%-9s %5s %10.10s %4s   %7s %18s %9s %10s %s\n", names[0], names[1],
         names[2], names[3], names[4], names[5], names[6], names[7], names[8]);
  TT.shown_header = 1;
}

static void print_info(void *data)
{
  struct file_info *fi = data;

  if (toys.optc && !filter_matches(fi)) return;

  if (toys.optflags&FLAG_t) {
    if (fi->pi.pid != TT.last_shown_pid)
      printf("%d\n", (TT.last_shown_pid = fi->pi.pid));
  } else {
    if (!TT.shown_header) print_header();
    printf("%-9s %5d %10.10s %4s%c%c %7s %18s %9s %10s %s\n",
           fi->pi.cmd, fi->pi.pid, fi->pi.user,
           fi->fd, fi->rw, fi->locks, fi->type, fi->device, fi->size_off,
           fi->node, fi->name);
  }

  if (CFG_FREE) {
    free(((struct file_info *)data)->name);
    free(data);
  }
}

static void fill_flags(struct file_info *fi)
{
  FILE* fp;
  long long pos;
  unsigned flags;

  snprintf(toybuf, sizeof(toybuf), "/proc/%d/fdinfo/%s", fi->pi.pid, fi->fd);
  fp = fopen(toybuf, "r");
  if (!fp) return;

  if (fscanf(fp, "pos: %lld flags: %o", &pos, &flags) == 2) {
    flags &= O_ACCMODE;
    if (flags == O_RDONLY) fi->rw = 'r';
    else if (flags == O_WRONLY) fi->rw = 'w';
    else fi->rw = 'u';

    snprintf(fi->size_off, sizeof(fi->size_off), "0t%lld", pos);
  }
  fclose(fp);
}

static int scan_proc_net_file(char *path, int family, char type,
    void (*fn)(char *, int, char, struct file_info *, long),
    struct file_info *fi, long sought_inode)
{
  FILE *fp = fopen(path, "r");
  char *line = NULL;
  size_t line_length = 0;

  if (!fp) return 0;

  if (!getline(&line, &line_length, fp)) return 0; // Skip header.

  while (getline(&line, &line_length, fp) > 0) {
    fn(line, family, type, fi, sought_inode);
    if (fi->name != 0) break;
  }

  free(line);
  fclose(fp);

  return fi->name != 0;
}

static void match_unix(char *line, int af, char type, struct file_info *fi,
                       long sought_inode)
{
  long inode;
  int path_pos;

  if (sscanf(line, "%*p: %*X %*X %*X %*X %*X %lu %n", &inode, &path_pos) >= 1 &&
        inode == sought_inode) {
    char *name = chomp(line + path_pos);

    strcpy(fi->type, "unix");
    fi->name = strdup(*name ? name : "socket");
  }
}

static void match_netlink(char *line, int af, char type, struct file_info *fi,
                          long sought_inode)
{
  unsigned state;
  long inode;
  char *netlink_states[] = {
    "ROUTE", "UNUSED", "USERSOCK", "FIREWALL", "SOCK_DIAG", "NFLOG", "XFRM",
    "SELINUX", "ISCSI", "AUDIT", "FIB_LOOKUP", "CONNECTOR", "NETFILTER",
    "IP6_FW", "DNRTMSG", "KOBJECT_UEVENT", "GENERIC", "DM", "SCSITRANSPORT",
    "ENCRYPTFS", "RDMA", "CRYPTO"
  };

  if (sscanf(line, "%*p %u %*u %*x %*u %*u %*u %*u %*u %lu",
             &state, &inode) < 2 || inode != sought_inode) {
    return;
  }

  strcpy(fi->type, "netlink");
  fi->name =
      strdup(state < ARRAY_LEN(netlink_states) ? netlink_states[state] : "?");
}

static void match_ip(char *line, int af, char type, struct file_info *fi,
                     long sought_inode)
{
  char *tcp_states[] = {
    "UNKNOWN", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
    "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING"
  };
  char local_ip[INET6_ADDRSTRLEN] = {0};
  char remote_ip[INET6_ADDRSTRLEN] = {0};
  struct in6_addr local, remote;
  int local_port, remote_port, state;
  long inode;
  int ok;

  if (af == 4) {
    ok = sscanf(line, " %*d: %x:%x %x:%x %x %*x:%*x %*X:%*X %*X %*d %*d %ld",
                &(local.s6_addr32[0]), &local_port,
                &(remote.s6_addr32[0]), &remote_port,
                &state, &inode) == 6;
  } else {
    ok = sscanf(line, " %*d: %8x%8x%8x%8x:%x %8x%8x%8x%8x:%x %x "
                "%*x:%*x %*X:%*X %*X %*d %*d %ld",
                &(local.s6_addr32[0]), &(local.s6_addr32[1]),
                &(local.s6_addr32[2]), &(local.s6_addr32[3]),
                &local_port,
                &(remote.s6_addr32[0]), &(remote.s6_addr32[1]),
                &(remote.s6_addr32[2]), &(remote.s6_addr32[3]),
                &remote_port, &state, &inode) == 12;
  }
  if (!ok || inode != sought_inode) return;

  strcpy(fi->type, af == 4 ? "IPv4" : "IPv6");
  inet_ntop(af, &local, local_ip, sizeof(local_ip));
  inet_ntop(af, &remote, remote_ip, sizeof(remote_ip));
  if (type == 't') {
    if (state < 0 || state > TCP_CLOSING) state = 0;
    fi->name = xmprintf(af == 4 ?
                        "TCP %s:%d->%s:%d (%s)" :
                        "TCP [%s]:%d->[%s]:%d (%s)",
                        local_ip, local_port, remote_ip, remote_port,
                        tcp_states[state]);
  } else {
    fi->name = xmprintf(af == 4 ? "%s %s:%d->%s:%d" : "%s [%s]:%d->[%s]:%d",
                        type == 'u' ? "UDP" : "RAW",
                        local_ip, local_port, remote_ip, remote_port);
  }
}

static int find_socket(struct file_info *fi, long inode)
{
  // TODO: other protocols (packet).
  return scan_proc_net_file("/proc/net/tcp", 4, 't', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/tcp6", 6, 't', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/udp", 4, 'u', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/udp6", 6, 'u', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/raw", 4, 'r', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/raw6", 6, 'r', match_ip, fi, inode) ||
    scan_proc_net_file("/proc/net/unix", 0, 0, match_unix, fi, inode) ||
    scan_proc_net_file("/proc/net/netlink", 0, 0, match_netlink, fi, inode);
}

static void fill_stat(struct file_info *fi, const char* path)
{
  struct stat sb;
  long dev;

  if (stat(path, &sb)) return;

  // Fill TYPE.
  switch ((sb.st_mode & S_IFMT)) {
    case S_IFBLK: strcpy(fi->type, "BLK"); break;
    case S_IFCHR: strcpy(fi->type, "CHR"); break;
    case S_IFDIR: strcpy(fi->type, "DIR"); break;
    case S_IFIFO: strcpy(fi->type, "FIFO"); break;
    case S_IFLNK: strcpy(fi->type, "LINK"); break;
    case S_IFREG: strcpy(fi->type, "REG"); break;
    case S_IFSOCK: strcpy(fi->type, "sock"); break;
    default:
      snprintf(fi->type, sizeof(fi->type), "0%03o", sb.st_mode & S_IFMT);
      break;
  }

  if (S_ISSOCK(sb.st_mode)) find_socket(fi, sb.st_ino);

  // Fill DEVICE.
  dev = (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode)) ? sb.st_rdev : sb.st_dev;
  if (!S_ISSOCK(sb.st_mode))
    snprintf(fi->device, sizeof(fi->device), "%ld,%ld",
             (long)major(dev), (long)minor(dev));

  // Fill SIZE/OFF.
  if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode))
    snprintf(fi->size_off, sizeof(fi->size_off), "%lld",
             (long long)sb.st_size);

  // Fill NODE.
  snprintf(fi->node, sizeof(fi->node), "%ld", (long)sb.st_ino);

  // Stash st_dev and st_ino for filtering.
  fi->st_dev = sb.st_dev;
  fi->st_ino = sb.st_ino;
}

struct file_info *new_file_info(struct proc_info *pi, const char* fd)
{
  struct file_info *fi = xzalloc(sizeof(struct file_info));

  dlist_add_nomalloc(&TT.files, (struct double_list *)fi);

  fi->pi = *pi;

  // Defaults.
  strcpy(fi->fd, fd);
  strcpy(fi->type, "unknown");
  fi->rw = fi->locks = ' ';

  return fi;
}

static void visit_symlink(struct proc_info *pi, char* name, char* path)
{
  struct file_info *fi = new_file_info(pi, "");

  // Get NAME.
  if (name) { // "/proc/pid/[cwd]".
    snprintf(fi->fd, sizeof(fi->fd), "%s", name);
    snprintf(toybuf, sizeof(toybuf), "/proc/%d/%s", pi->pid, path);
  } else { // "/proc/pid/fd/[3]"
    snprintf(fi->fd, sizeof(fi->fd), "%s", path);
    fill_flags(fi); // Clobbers toybuf.
    snprintf(toybuf, sizeof(toybuf), "/proc/%d/fd/%s", pi->pid, path);
  }
  // TODO: code called by fill_stat would be easier to write if we didn't
  // rely on toybuf being preserved here.
  fill_stat(fi, toybuf);
  if (!fi->name) { // We already have a name for things like sockets.
    fi->name = xreadlink(toybuf);
    if (!fi->name) {
      fi->name = xmprintf("%s (readlink: %s)", toybuf, strerror(errno));
    }
  }
}

static void visit_maps(struct proc_info *pi)
{
  FILE *fp;
  unsigned long long offset;
  char device[10];
  long inode;
  char *line = NULL;
  size_t line_length = 0;

  snprintf(toybuf, sizeof(toybuf), "/proc/%d/maps", pi->pid);
  fp = fopen(toybuf, "r");
  if (!fp) return;

  while (getline(&line, &line_length, fp) > 0) {
    int name_pos;

    if (sscanf(line, "%*x-%*x %*s %llx %s %ld %n",
               &offset, device, &inode, &name_pos) >= 3) {
      struct file_info *fi;

      // Ignore non-file maps.
      if (inode == 0 || !strcmp(device, "00:00")) continue;
      // TODO: show unique maps even if they have a non-zero offset?
      if (offset != 0) continue;

      fi = new_file_info(pi, "mem");
      fi->name = strdup(chomp(line + name_pos));
      fill_stat(fi, fi->name);
    }
  }
  free(line);
  fclose(fp);
}

static void visit_fds(struct proc_info *pi)
{
  DIR *dir;
  struct dirent *de;

  snprintf(toybuf, sizeof(toybuf), "/proc/%d/fd", pi->pid);
  if (!(dir = opendir(toybuf))) {
    struct file_info *fi = new_file_info(pi, "NOFD");

    fi->name = xmprintf("%s (opendir: %s)", toybuf, strerror(errno));
    return;
  }

  while ((de = readdir(dir))) {
    if (*de->d_name == '.') continue;
    visit_symlink(pi, NULL, de->d_name);
  }

  closedir(dir);
}

static void lsof_pid(int pid)
{
  struct proc_info pi;
  FILE *fp;
  char *line;
  struct stat sb;

  // Does this process even exist?
  snprintf(toybuf, sizeof(toybuf), "/proc/%d/stat", pid);
  fp = fopen(toybuf, "r");
  if (!fp) return;

  // Get COMMAND.
  strcpy(pi.cmd, "?");
  line = fgets(toybuf, sizeof(toybuf), fp);
  fclose(fp);
  if (line) {
    char *open_paren = strchr(toybuf, '(');
    char *close_paren = strrchr(toybuf, ')');

    if (open_paren && close_paren) {
      *close_paren = 0;
      snprintf(pi.cmd, sizeof(pi.cmd), "%s", open_paren + 1);
    }
  }

  // We already know PID.
  pi.pid = pid;

  // Get USER.
  snprintf(toybuf, sizeof(toybuf), "/proc/%d", pid);
  if (!stat(toybuf, &sb)) {
    struct passwd *pw;

    if (!(toys.optflags&FLAG_l) && (pw = getpwuid(sb.st_uid))) {
      snprintf(pi.user, sizeof(pi.user), "%s", pw->pw_name);
    } else snprintf(pi.user, sizeof(pi.user), "%u", (unsigned)sb.st_uid);
  }

  visit_symlink(&pi, "cwd", "cwd");
  visit_symlink(&pi, "rtd", "root");
  visit_symlink(&pi, "txt", "exe");
  visit_maps(&pi);
  visit_fds(&pi);
}

static int scan_slash_proc(struct dirtree *node)
{
  int pid;

  if (!node->parent) return DIRTREE_RECURSE;
  if ((pid = atol(node->name))) lsof_pid(pid);
  return 0;
}

void lsof_main(void)
{
  int i;

  // lsof will only filter on paths it can stat (because it filters by inode).
  TT.sought_files = xmalloc(toys.optc*sizeof(struct stat));
  for (i = 0; i < toys.optc; ++i) {
    xstat(toys.optargs[i], &(TT.sought_files[i]));
  }

  if (toys.optflags&FLAG_p) {
    char *pid_str;
    int length, pid;

    while ((pid_str = comma_iterate(&TT.pids, &length))) {
      pid_str[length] = 0;
      if (!(pid = atoi(pid_str))) error_exit("bad pid '%s'", pid_str);
      lsof_pid(pid);
    }
  } else dirtree_read("/proc", scan_slash_proc);

  llist_traverse(TT.files, print_info);
}
