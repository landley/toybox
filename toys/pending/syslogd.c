/* syslogd.c - a system logging utility.
 *
 * Copyright 2013 Madhur Verma <mad.flexi@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_SYSLOGD(NEWTOY(syslogd,">0l#<1>8=8R:b#<0>99=1s#<0=200m#<0>71582787=20O:p:f:a:nSKLD", TOYFLAG_SBIN|TOYFLAG_STAYROOT))

config SYSLOGD
  bool "syslogd"
  default n
  help
  Usage: syslogd  [-a socket] [-p socket] [-O logfile] [-f config file] [-m interval]
                  [-p socket] [-s SIZE] [-b N] [-R HOST] [-l N] [-nSLKD]

  System logging utility

  -a      Extra unix socket for listen
  -O FILE Default log file <DEFAULT: /var/log/messages>
  -f FILE Config file <DEFAULT: /etc/syslog.conf>
  -p      Alternative unix domain socket <DEFAULT : /dev/log>
  -n      Avoid auto-backgrounding.
  -S      Smaller output
  -m MARK interval <DEFAULT: 20 minutes> (RANGE: 0 to 71582787)
  -R HOST Log to IP or hostname on PORT (default PORT=514/UDP)"
  -L      Log locally and via network (default is network only if -R)"
  -s SIZE Max size (KB) before rotation (default:200KB, 0=off)
  -b N    rotated logs to keep (default:1, max=99, 0=purge)
  -K      Log to kernel printk buffer (use dmesg to read it)
  -l N    Log only messages more urgent than prio(default:8 max:8 min:1)
  -D      Drop duplicates
*/

#define FOR_syslogd
#define SYSLOG_NAMES
#include "toys.h"
#include "toynet.h"

GLOBALS(
  char *socket;
  char *config_file;
  char *unix_socket;
  char *logfile;
  long interval;
  long rot_size;
  long rot_count;
  char *remote_log;
  long log_prio;

  struct arg_list *lsocks;  // list of listen sockets
  struct arg_list *lfiles;  // list of write logfiles
  fd_set rfds;        // fds for reading
  int sd;            // socket for logging remote messeges.
)

#define flag_get(f,v,d)  ((toys.optflags & f) ? v : d)
#define flag_chk(f)    ((toys.optflags & f) ? 1 : 0)


// Signal handling 
struct fd_pair { int rd; int wr; };
static struct fd_pair sigfd;

// UNIX Sockets for listening
typedef struct unsocks_s {
  char *path;
  struct sockaddr_un sdu;
  int sd;
} unsocks_t;

// Log file entry to log into.
typedef struct logfile_s {
  char *filename;
  char *config;
  uint8_t isNetwork;
  uint32_t facility[8];
  uint8_t level[LOG_NFACILITIES];
  int logfd;
  struct sockaddr_in saddr;
} logfile_t;

// Adds opened socks to rfds for select()
static int addrfds(void)
{
  unsocks_t *sock;
  int ret = 0;
  struct arg_list *node = TT.lsocks;
  FD_ZERO(&TT.rfds);

  while (node) {
    sock = (unsocks_t*) node->arg;
    if (sock->sd > 2) {
      FD_SET(sock->sd, &TT.rfds);
      ret = sock->sd;
    }
    node = node->next;
  }
  FD_SET(sigfd.rd, &TT.rfds);
  return (sigfd.rd > ret)?sigfd.rd:ret;
}

/*
 * initializes unsock_t structure
 * and opens socket for reading
 * and adds to global lsock list.
 */
static int open_unix_socks(void)
{
  struct arg_list *node;
  unsocks_t *sock;
  int ret = 0;

  for(node = TT.lsocks; node; node = node->next) {
    sock = (unsocks_t*) node->arg;
    sock->sdu.sun_family = AF_UNIX;
    strcpy(sock->sdu.sun_path, sock->path);
    sock->sd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock->sd < 0) {
      perror_msg("OPEN SOCKS : failed");
      continue;
    }
    unlink(sock->sdu.sun_path);
    if (bind(sock->sd, (struct sockaddr *) &sock->sdu, sizeof(sock->sdu))) {
      perror_msg("BIND SOCKS : failed sock : %s", sock->sdu.sun_path);
      close(sock->sd);
      continue;
    }
    chmod(sock->path, 0777);
    ret++;
  }
  return ret;
}

/*
 * creates a socket of family INET and protocol UDP
 * if successful then returns SOCK othrwise error
 */
static int open_udp_socks(char *host, int port, struct sockaddr_in *sadd)
{
  struct addrinfo *info, rp;

  memset(&rp, 0, sizeof(rp));
  rp.ai_family = AF_INET;
  rp.ai_socktype = SOCK_DGRAM;
  rp.ai_protocol = IPPROTO_UDP;

  if (getaddrinfo(host, NULL, &rp, &info) || !info) 
    perror_exit("BAD ADDRESS: can't find : %s ", host);
  ((struct sockaddr_in*)info->ai_addr)->sin_port = htons(port);
  memcpy(sadd, info->ai_addr, info->ai_addrlen);
  freeaddrinfo(info);

  return xsocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

// Returns node having filename
static struct arg_list *get_file_node(char *filename, struct arg_list *list)
{
  while (list) {
    if (!strcmp(((logfile_t*) list->arg)->filename, filename)) return list;
    list = list->next;
  }
  return list;
}

/*
 * recurses the logfile list and resolves config
 * for evry file and updates facilty and log level bits.
 */
static int resolve_config(logfile_t *file)
{
  char *tk, *fac, *lvl, *tmp, *nfac;
  int count = 0;
  unsigned facval = 0;
  uint8_t set, levval, neg;
  CODE *val = NULL;

  tmp = xstrdup(file->config);
  for (tk = strtok(tmp, "; \0"); tk; tk = strtok(NULL, "; \0")) {
    fac = tk;
    tk = strchr(fac, '.');
    if (!tk) return -1;
    *tk = '\0';
    lvl = tk + 1;

    while(1) {
      count = 0;
      if (*fac == '*') {
        facval = 0xFFFFFFFF;
        fac++;
      }
      nfac = strchr(fac, ',');
      if (nfac) *nfac = '\0';
      while (*fac && ((CODE*) &facilitynames[count])->c_name) {
        val = (CODE*) &facilitynames[count];
        if (!strcmp(fac, val->c_name)) {
          facval |= (1<<LOG_FAC(val->c_val));
          break;
        }
        count++;
      }
      if (((CODE*) &facilitynames[count])->c_val == -1)
        return -1;

      if (nfac) fac = nfac+1;
      else break;
    }

    count = 0;
    set = 0;
    levval = 0;
    neg = 0;
    if (*lvl == '!') {
      neg = 1;
      lvl++;
    }
    if (*lvl == '=') {
      set = 1;
      lvl++;
    }
    if (*lvl == '*') {
      levval = 0xFF;
      lvl++;
    }
    while (*lvl && ((CODE*) &prioritynames[count])->c_name) {
      val = (CODE*) &prioritynames[count];
      if (!strcmp(lvl, val->c_name)) {
        levval |= set ? LOG_MASK(val->c_val):LOG_UPTO(val->c_val);
        if (neg) levval = ~levval;
        break;
      }
      count++;
    }
    if (((CODE*) &prioritynames[count])->c_val == -1) return -1;

    count = 0;
    set = levval;
    while(set) {
      if (set & 0x1) file->facility[count] |= facval;
      set >>= 1;
      count++;
    }
    for (count = 0; count < LOG_NFACILITIES; count++) {
      if (facval & 0x1) file->level[count] |= levval;
      facval >>= 1;
    }
  }
  free(tmp);

  return 0;
}

// Parse config file and update the log file list.
static int parse_config_file(void)
{
  logfile_t *file;
  FILE *fp = NULL;
  char *confline = NULL, *tk = NULL, *tokens[2] = {NULL, NULL};
  int len, linelen, tcount, lineno = 0;
  struct arg_list *node;
  /*
   * if -K then open only /dev/kmsg
   * all other log files are neglected
   * thus no need to open config either.
   */
  if (flag_chk(FLAG_K)) {
    node = xzalloc(sizeof(struct arg_list));
    file = xzalloc(sizeof(logfile_t));
    file->filename = "/dev/kmsg";
    file->config = "*.*";
    memset(file->level, 0xFF, sizeof(file->level));
    memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
    node->arg = (char*) file;
    TT.lfiles = node;
    return 0;
  }
  /*
   * if -R then add remote host to log list
   * if -L is not provided all other log
   * files are neglected thus no need to
   * open config either so just return.
   */
   if (flag_chk(FLAG_R)) {
     node = xzalloc(sizeof(struct arg_list));
     file = xzalloc(sizeof(logfile_t));
     file->filename = xmsprintf("@%s",TT.remote_log);
     file->isNetwork = 1;
     file->config = "*.*";
     memset(file->level, 0xFF, sizeof(file->level));
     memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
     node->arg = (char*) file;
     TT.lfiles = node;
     if (!flag_chk(FLAG_L))return 0;
   }
  /*
   * Read config file and add logfiles to the list
   * with their configuration.
   */
  fp = fopen(TT.config_file, "r");
  if (!fp && flag_chk(FLAG_f))
    perror_exit("can't open '%s'", TT.config_file);

  for (len = 0, linelen = 0; fp;) {
    len = getline(&confline, (size_t*) &linelen, fp);
    if (len <= 0) break;
    lineno++;
    for (; *confline == ' '; confline++, len--) ;
    if ((confline[0] == '#') || (confline[0] == '\n')) continue;
    for (tcount = 0, tk = strtok(confline, " \t"); tk && (tcount < 2); tk =
        strtok(NULL, " \t"), tcount++) {
      if (tcount == 2) {
        error_msg("error in '%s' at line %d", TT.config_file, lineno);
        return -1;
      }
      tokens[tcount] = xstrdup(tk);
    }
    if (tcount <= 1 || tcount > 2) {
      if (tokens[0]) free(tokens[0]);
      error_msg("bad line %d: 1 tokens found, 2 needed", lineno);
      return -1;
    }
    tk = (tokens[1] + (strlen(tokens[1]) - 1));
    if (*tk == '\n') *tk = '\0';
    if (*tokens[1] == '\0') {
      error_msg("bad line %d: 1 tokens found, 2 needed", lineno);
      return -1;
    }
    if (*tokens[1] == '*') goto loop_again;

    node = get_file_node(tokens[1], TT.lfiles);
    if (!node) {
      node = xzalloc(sizeof(struct arg_list));
      file = xzalloc(sizeof(logfile_t));
      file->config = xstrdup(tokens[0]);
      if (resolve_config(file)==-1) {
        error_msg("error in '%s' at line %d", TT.config_file, lineno);
        return -1;
      }
      file->filename = xstrdup(tokens[1]);
      if (*file->filename == '@') file->isNetwork = 1;
      node->arg = (char*) file;
      node->next = TT.lfiles;
      TT.lfiles = node;
    } else {
      file = (logfile_t*) node->arg;
      int rel = strlen(file->config) + strlen(tokens[0]) + 2;
      file->config = xrealloc(file->config, rel);
      sprintf(file->config, "%s;%s", file->config, tokens[0]);
    }
loop_again:
    if (tokens[0]) free(tokens[0]);
    if (tokens[1]) free(tokens[1]);
    free(confline);
    confline = NULL;
  }
  /*
   * Can't open config file or support is not enabled
   * adding default logfile to the head of list.
   */
  if (!fp){
    node = xzalloc(sizeof(struct arg_list));
    file = xzalloc(sizeof(logfile_t));
    file->filename = flag_get(FLAG_O, TT.logfile, "/var/log/messages"); //DEFLOGFILE
    file->isNetwork = 0;
    file->config = "*.*";
    memset(file->level, 0xFF, sizeof(file->level));
    memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
    node->arg = (char*) file;
    node->next = TT.lfiles;
    TT.lfiles = node;
  }
  if (fp) {
    fclose(fp);
    fp = NULL;
  }
  return 0;
}

static int getport(char *str, char *filename)
{
  char *endptr = NULL;
  int base = 10;
  errno = 0;
  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    base = 16;
    str += 2;
  }
  long port = strtol(str, &endptr, base);
  if (errno || *endptr!='\0'|| endptr == str 
    || port < 0 || port > 65535) error_exit("wrong port no in %s", filename);
  return (int)port;
}

// open every log file in list.
static void open_logfiles(void)
{
  logfile_t *tfd;
  char *p, *tmpfile;
  int port = -1;
  struct arg_list *node = TT.lfiles;

  while (node) {
    tfd = (logfile_t*) node->arg;
    if (tfd->isNetwork) {
      tmpfile = xstrdup(tfd->filename +1);
      if ((p = strchr(tmpfile, ':'))) {
        *p = '\0';
        port = getport(p + 1, tfd->filename);
      }
      tfd->logfd = open_udp_socks(tmpfile, (port>=0)?port:514, &tfd->saddr);
      free(tmpfile);
    } else tfd->logfd = open(tfd->filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (tfd->logfd < 0) {
      tfd->filename = "/dev/console";
      tfd->logfd = open(tfd->filename, O_APPEND);
    }
    node = node->next;
  }
}

//write to file with rotation
static int write_rotate( logfile_t *tf, int len)
{
  int size, isreg;
  struct stat statf;
  isreg = (!fstat(tf->logfd, &statf) && S_ISREG(statf.st_mode));
  size = statf.st_size;

  if (flag_chk(FLAG_s) || flag_chk(FLAG_b)) {
    if (TT.rot_size && isreg && (size + len) > (TT.rot_size*1024)) {
      if (TT.rot_count) { /* always 0..99 */
        int i = strlen(tf->filename) + 3 + 1;
        char old_file[i];
        char new_file[i];
        i = TT.rot_count - 1;
        while (1) {
          sprintf(new_file, "%s.%d", tf->filename, i);
          if (!i) break;
          sprintf(old_file, "%s.%d", tf->filename, --i);
          rename(old_file, new_file);
        }
        rename(tf->filename, new_file);
        unlink(tf->filename);
        close(tf->logfd);
        tf->logfd = open(tf->filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
      }
      ftruncate(tf->logfd, 0);
    }
  }
  return write(tf->logfd, toybuf, len);
}

// Lookup numerical code from name
// Only used in logger
int logger_lookup(int where, char *key)
{
  CODE *w = ((CODE *[]){facilitynames, prioritynames})[where];

  for (; w->c_name; w++)
    if (!strcasecmp(key, w->c_name)) return w->c_val;

  return -1;
}

//search the given name and return its value
static char *dec(int val, CODE *clist)
{
  const CODE *c;

  for (c = clist; c->c_name; c++) 
    if (val == c->c_val) return c->c_name;
  return itoa(val);
}

// Compute priority from "facility.level" pair
static void priority_to_string(int pri, char **facstr, char **lvlstr)
{
  int fac,lev;

  fac = LOG_FAC(pri);
  lev = LOG_PRI(pri);
  *facstr = dec(fac<<3, facilitynames);
  *lvlstr = dec(lev, prioritynames);
}

//Parse messege and write to file.
static void logmsg(char *msg, int len)
{
  time_t now;
  char *p, *ts, *lvlstr, *facstr;
  struct utsname uts;
  int pri = 0;
  struct arg_list *lnode = TT.lfiles;

  char *omsg = msg;
  int olen = len, fac, lvl;
  
  if (*msg == '<') { // Extract the priority no.
    pri = (int) strtoul(msg + 1, &p, 10);
    if (*p == '>') msg = p + 1;
  }
  /* Jan 18 00:11:22 msg...
   * 01234567890123456
   */
  if (len < 16 || msg[3] != ' ' || msg[6] != ' ' || msg[9] != ':'
      || msg[12] != ':' || msg[15] != ' ') {
    time(&now);
    ts = ctime(&now) + 4; /* skip day of week */
  } else {
    now = 0;
    ts = msg;
    msg += 16;
  }
  ts[15] = '\0';
  fac = LOG_FAC(pri);
  lvl = LOG_PRI(pri);

  if (flag_chk(FLAG_K)) {
    len = sprintf(toybuf, "<%d> %s\n", pri, msg);
    goto do_log;
  }
  priority_to_string(pri, &facstr, &lvlstr);

  p = "local";
  if (!uname(&uts)) p = uts.nodename;
  if (flag_chk(FLAG_S)) len = sprintf(toybuf, "%s %s\n", ts, msg);
  else len = sprintf(toybuf, "%s %s %s.%s %s\n", ts, p, facstr, lvlstr, msg);

do_log:
  if (lvl >= TT.log_prio) return;

  while (lnode) {
    logfile_t *tf = (logfile_t*) lnode->arg;
    if (tf->logfd > 0) {
      if ((tf->facility[lvl] & (1 << fac)) && (tf->level[fac] & (1<<lvl))) {
        int wlen;
        if (tf->isNetwork)
          wlen = sendto(tf->logfd, omsg, olen, 0, (struct sockaddr*)&tf->saddr, sizeof(tf->saddr));
        else wlen = write_rotate(tf, len);
        if (wlen < 0) perror_msg("write failed file : %s ", (tf->isNetwork)?(tf->filename+1):tf->filename);
      }
    }
    lnode = lnode->next;
  }
}

/*
 * closes all read and write fds
 * and frees all nodes and lists
 */
static void cleanup(void)
{
  struct arg_list *fnode;
  while (TT.lsocks) {
    fnode = TT.lsocks;
    if (((unsocks_t*) fnode->arg)->sd >= 0)
      close(((unsocks_t*) fnode->arg)->sd);
    free(fnode->arg);
    TT.lsocks = fnode->next;
    free(fnode);
  }
  unlink("/dev/log");

  while (TT.lfiles) {
    fnode = TT.lfiles;
    if (((logfile_t*) fnode->arg)->logfd >= 0)
      close(((logfile_t*) fnode->arg)->logfd);
    free(fnode->arg);
    TT.lfiles = fnode->next;
    free(fnode);
  }
}

static void signal_handler(int sig)
{
  unsigned char ch = sig;
  if (write(sigfd.wr, &ch, 1) != 1) error_msg("can't send signal");
}

static void setup_signal()
{
  if (pipe((int *)&sigfd) < 0) error_exit("pipe failed\n");

  fcntl(sigfd.wr , F_SETFD, FD_CLOEXEC);
  fcntl(sigfd.rd , F_SETFD, FD_CLOEXEC);
  int flags = fcntl(sigfd.wr, F_GETFL);
  fcntl(sigfd.wr, F_SETFL, flags | O_NONBLOCK);
  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);
}

void syslogd_main(void)
{
  unsocks_t *tsd;
  int maxfd, retval, last_len=0;
  struct timeval tv;
  struct arg_list *node;
  char *temp, *buffer = (toybuf +2048), *last_buf = (toybuf + 3072); //these two buffs are of 1K each

  if (flag_chk(FLAG_p) && strlen(TT.unix_socket) > 108)
    error_exit("Socket path should not be more than 108");

  TT.config_file = flag_get(FLAG_f, TT.config_file, "/etc/syslog.conf"); //DEFCONFFILE
init_jumpin:
  TT.lsocks = xzalloc(sizeof(struct arg_list));
  tsd = xzalloc(sizeof(unsocks_t));

  tsd->path = flag_get(FLAG_p, TT.unix_socket , "/dev/log"); // DEFLOGSOCK
  TT.lsocks->arg = (char*) tsd;

  if (flag_chk(FLAG_a)) {
    for (temp = strtok(TT.socket, ":"); temp; temp = strtok(NULL, ":")) {
      struct arg_list *ltemp = xzalloc(sizeof(struct arg_list));
      if (strlen(temp) > 107) temp[108] = '\0';
      tsd = xzalloc(sizeof(unsocks_t));
      tsd->path = temp;
      ltemp->arg = (char*) tsd;
      ltemp->next = TT.lsocks;
      TT.lsocks = ltemp;
    }
  }
  if (!open_unix_socks()) {
    error_msg("Can't open single socket for listenning.");
    goto clean_and_exit;
  }
  setup_signal();
  if (parse_config_file() == -1) goto clean_and_exit;
  open_logfiles();
  if (!flag_chk(FLAG_n)) {
    //don't daemonize again if SIGHUP received.
    toys.optflags |= FLAG_n;
  }
  {
    int pid_fd = open("/var/run/syslogd.pid", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (pid_fd > 0) {
      unsigned pid = getpid();
      int len = sprintf(toybuf, "%u\n", pid);
      write(pid_fd, toybuf, len);
      close(pid_fd);
    }
  }

  logmsg("<46>Toybox: syslogd started", 27); //27 : the length of message
  for (;;) {
    maxfd = addrfds();
    tv.tv_usec = 0;
    tv.tv_sec = TT.interval*60;

    retval = select(maxfd + 1, &TT.rfds, NULL, NULL, (TT.interval)?&tv:NULL);
    if (retval < 0) { /* Some error. */
      if (errno == EINTR) continue;
      perror_msg("Error in select ");
      continue;
    }
    if (!retval) { /* Timed out */
      logmsg("<46>-- MARK --", 14);
      continue;
    }
    if (FD_ISSET(sigfd.rd, &TT.rfds)) { /* May be a signal */
      unsigned char sig;

      if (read(sigfd.rd, &sig, 1) != 1) {
        error_msg("signal read failed.\n");
        continue;
      }
      switch(sig) {
        case SIGTERM:    /* FALLTHROUGH */
        case SIGINT:     /* FALLTHROUGH */
        case SIGQUIT:
          logmsg("<46>syslogd exiting", 19);
          if (CFG_TOYBOX_FREE ) cleanup();
          signal(sig, SIG_DFL);
          sigset_t ss;
          sigemptyset(&ss);
          sigaddset(&ss, sig);
          sigprocmask(SIG_UNBLOCK, &ss, NULL);
          raise(sig);
          _exit(1);  /* Should not reach it */
          break;
        case SIGHUP:
          logmsg("<46>syslogd exiting", 19);
          cleanup(); //cleanup is done, as we restart syslog.
          goto init_jumpin;
        default: break;
      }
    }
    if (retval > 0) { /* Some activity on listen sockets. */
      node = TT.lsocks;
      while (node) {
        int sd = ((unsocks_t*) node->arg)->sd;
        if (FD_ISSET(sd, &TT.rfds)) {
          int len = read(sd, buffer, 1023); //buffer is of 1K, hence readingonly 1023 bytes, 1 for NUL
          if (len > 0) {
            buffer[len] = '\0';
            if(flag_chk(FLAG_D) && (len == last_len))
              if (!memcmp(last_buf, buffer, len)) break;

            memcpy(last_buf, buffer, len);
            last_len = len;
            logmsg(buffer, len);
          }
          break;
        }
        node = node->next;
      }
    }
  }
clean_and_exit:
  logmsg("<46>syslogd exiting", 19);
  if (CFG_TOYBOX_FREE ) cleanup();
}
