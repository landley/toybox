/* top.c - Provide a view of process activity in real time.
 *
 * Copyright 2013 Bilal Qureshi <bilal.jmi@gmail.com>
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_TOP(NEWTOY(top, ">0d#=3n#<1mb", TOYFLAG_USR|TOYFLAG_BIN))

config TOP
  bool "top"
  default n
  help
    
    usage: top [-mb] [ -d seconds ] [ -n iterations ]

    Provide a view of process activity in real time.
    Keys
       N/M/P/T show CPU usage, sort by pid/mem/cpu/time
       S       show memory
       R       reverse sort
       H       toggle threads
       C,1     toggle SMP
       Q,^C    exit

    Options
       -n Iterations before exiting
       -d Delay between updates
       -m Same as 's' key
       -b Batch mode
*/

#define FOR_top
#include "toys.h"
#include <signal.h>
#include <poll.h>

GLOBALS(
  long iterations;
  long delay;

  long cmp_field;
  long reverse;
  long rows;
  long smp;
  long threads;
  long m_flag;
  long num_new_procs;
  long scroll_offset;
  struct termios inf;
)

#define PROC_NAME_LEN 512 //For long cmdline.
#define INIT_PROCS 50

struct cpu_info {
  long unsigned utime, ntime, stime, itime;
  long unsigned iowtime, irqtime, sirqtime, steal;
  unsigned long long total;
};

enum CODE{
  KEY_UP = 0x100, KEY_DOWN, KEY_HOME,
  KEY_END, KEY_PAGEUP, KEY_PAGEDN,
};

struct keycode_map_s {
  char *key;
  int code;
};

struct proc_info {
  struct proc_info *next;
  pid_t pid, ppid;
  uid_t uid;
  char name[PROC_NAME_LEN];
  char tname[PROC_NAME_LEN];
  char state[4];
  int prs;
  unsigned long utime, stime, delta_utime, delta_stime, delta_time;
  unsigned long vss, vssrw, rss, rss_shr, drt, drt_shr, stack;
};

static struct proc_info *free_procs, **old_procs, **new_procs;
static struct cpu_info old_cpu[10], new_cpu[10]; //1 total, 8 cores, 1 null
static int (*proc_cmp)(const void *a, const void *b);

static struct proc_info *find_old_proc(pid_t pid) 
{
  int i;

  for (i = 0; old_procs && old_procs[i]; i++)
    if (old_procs[i]->pid == pid) return old_procs[i];

  return NULL;
}

static void read_stat(char *filename, struct proc_info *proc) 
{
  int nice;
  FILE *file;
  char *open_paren, *close_paren;

  if (!(file = fopen(filename, "r"))) return;
  fgets(toybuf, sizeof(toybuf), file);
  fclose(file);

  // Split at first '(' and last ')' to get process name.
  open_paren = strchr(toybuf, '(');
  close_paren = strrchr(toybuf, ')');
  if (!open_paren || !close_paren) return;

  *open_paren = *close_paren = '\0';
  snprintf(proc->tname, PROC_NAME_LEN, "[%s]",open_paren + 1);

  // Scan rest of string.
  sscanf(close_paren + 1, " %c %d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
      "%lu %lu %*d %*d %*d %d %*d %*d %*d %lu %ld "
      "%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %d",
      &proc->state[0], &proc->ppid, &proc->utime, &proc->stime, &nice, 
      &proc->vss, &proc->rss, &proc->prs);
  if (!proc->vss && proc->state[0] != 'Z') proc->state[1] = 'W';
  else proc->state[1] = ' ';
  if (nice < 0 ) proc->state[2] = '<';
  else if (nice) proc->state[2] = 'N';
  else proc->state[2] = ' ';
}

static void read_status(char *filename, struct proc_info *proc) 
{
  FILE *file;

  if (!(file = fopen(filename, "r"))) return;
  while (fgets(toybuf, sizeof(toybuf), file)) 
    if (sscanf(toybuf, "Uid: %u", &(proc->uid)) == 1) break;

  fclose(file);
}

static void read_cmdline(char *filename, struct proc_info *proc) 
{
  int fd, len, rbytes = 0;
  char *ch, *base, tname[PROC_NAME_LEN];

  if ((fd = open(filename, O_RDONLY)) == -1) return;
  rbytes = readall(fd, toybuf, sizeof(toybuf));
  close(fd);
  if (rbytes <= 0) {
    strcpy(proc->name, proc->tname);
    return;
  }
  toybuf[rbytes] = '\0';
  while (--rbytes >= 0 && toybuf[rbytes] == '\0') continue;

  snprintf(tname, PROC_NAME_LEN, "%s", proc->tname+1);
  tname[strlen(tname) - 1] = '\0';
  ch = strchr(toybuf, ' ');
  if (ch) *ch = '\0';
  base = strrchr(toybuf, '/');
  if (base) base++;
  else base = toybuf;

  for (; rbytes >= 0; rbytes--)
    if ((unsigned char)toybuf[rbytes] < ' ') toybuf[rbytes] = ' ';

  if (*base == '-') base++;
  len = strlen(tname);
  if (strncmp(base, tname, len)) {
    len +=3; //{,}, \0
    rbytes = strlen(toybuf);
    memmove(toybuf+ len, toybuf, rbytes+1);
    snprintf(toybuf, sizeof(toybuf), "{%s}", tname);
    toybuf[len-1] = ' ';
  } 
  snprintf(proc->name, PROC_NAME_LEN, "%s", toybuf);
}

static void add_proc(int proc_num, struct proc_info *proc) 
{
  int i;

  if (proc_num >= TT.num_new_procs-1) {
    new_procs = xrealloc(new_procs, (INIT_PROCS + TT.num_new_procs) 
        * sizeof(struct proc_info *));
    for (i = TT.num_new_procs; i < (INIT_PROCS +  TT.num_new_procs); i++)
      new_procs[i] = NULL;
    TT.num_new_procs += INIT_PROCS;
  }
  new_procs[proc_num] = proc;
}

void signal_handler(int sig)
{
  tcsetattr(STDIN_FILENO, TCSANOW, &TT.inf);
  xputc('\n');
  signal(sig, SIG_DFL);
  raise(sig);
  _exit(sig | 128);
}

static int get_key_code(char *ch, int i)
{  
  static struct keycode_map_s type2[] = {
    {"OA",KEY_UP}, {"OB",KEY_DOWN}, {"OH",KEY_HOME},
    {"OF",KEY_END}, {"[A",KEY_UP}, {"[B",KEY_DOWN},
    {"[H",KEY_HOME}, {"[F",KEY_END}, {NULL, 0}
  };       

  static struct keycode_map_s type3[] = {
    {"[1~", KEY_HOME}, {"[4~", KEY_END}, {"[5~", KEY_PAGEUP},
    {"[6~", KEY_PAGEDN}, {"[7~", KEY_HOME}, {"[8~", KEY_END},
    {NULL, 0}
  };
  struct keycode_map_s *table, *keytable[3] = {type2, type3, NULL};
  int j;

  if ( i > 3 || i < 1) return -1; 

  for (j=0; (table = keytable[j]); j++) {
    while (table->key) {
      if (!strncmp(ch, table->key, i)) break;
      table++;
    }
    if (table->key) {
      if (i == 1 || (i == 2 && j)) return 1;
      return table->code;
    }
  }
  return -1;
}

static int read_input(int delay)
{
  struct pollfd pfd[1];
  int ret, fret = 0, cnt = 0, escproc = 0, timeout = delay * 1000;
  char ch, seq[4] = {0,};
  struct termios newf;

  tcgetattr(0, &TT.inf);
  if (toys.optflags & FLAG_b) {
    sleep(delay);
    return 0;
  }
  pfd[0].fd = 0;
  pfd[0].events = POLLIN;

  //prepare terminal for input, without Enter of Carriage return
  memcpy(&newf, &TT.inf, sizeof(struct termios));
  newf.c_lflag &= ~(ICANON | ECHO | ECHONL);
  newf.c_cc[VMIN] = 1;
  newf.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &newf);

  while (1) {
    if ((ret = poll(pfd, 1, timeout)) >= 0) break;
    else {
      if (timeout > 0) timeout--;
      if (errno == EINTR) continue;
      perror_exit("poll");
    }
  }

  while (ret) {
    if (read(STDIN_FILENO, &ch, 1) != 1) toys.optflags |= FLAG_b;
    else if (ch == '\033' || escproc) {
      int code;
      //process ESC keys
      if (!escproc) {
        if (!poll(pfd, 1, 50)) break; //no more chars
        escproc = 1;
        continue;
      }
      seq[cnt++] = ch;
      code = get_key_code(seq, cnt);
      switch(code) {
        case -1: //no match
          fret = 0;
          break;
        case 1: //read more
          continue;
        default: // got the key
          fret = code;
          break;
      }
    } else if ((ch == TT.inf.c_cc[VINTR]) 
        || (ch == TT.inf.c_cc[VEOF]))
      fret = 'q';
    else fret = ch | 0x20;
    break;
  }
  tcsetattr(0, TCSANOW, &TT.inf);
  return fret;
}

// Allocation for Processes
static struct proc_info *alloc_proc(void) 
{
  struct proc_info *proc;

  if (free_procs) {
    proc = free_procs;
    free_procs = free_procs->next;
    memset(proc, 0, sizeof(*proc));
  } else proc = xzalloc(sizeof(*proc));

  return proc;
}

static void free_proc_list(struct proc_info *procs)
{
  struct proc_info *tmp = procs;
  
  for (;tmp; tmp = procs) {
    procs = procs->next;
    free(tmp);
  }
}

// Free allocated Processes in order to avoid memory leaks
static void free_proc(struct proc_info *proc) 
{
  proc->next = free_procs;
  free_procs = proc;
}

static struct proc_info *add_new_proc(pid_t pid, pid_t tid)
{
  char filename[64];
  struct proc_info *proc = alloc_proc();

  proc->pid = (tid)? tid : pid;
  if (!tid) {
    sprintf(filename, "/proc/%d/stat", pid);
    read_stat(filename, proc);
    sprintf(filename, "/proc/%d/cmdline", pid);
    read_cmdline(filename, proc);
    sprintf(filename, "/proc/%d/status", pid);
    read_status(filename, proc);
  } else{
    sprintf(filename, "/proc/%d/task/%d/stat", pid,tid);
    read_stat(filename, proc);
    sprintf(filename, "/proc/%d/task/%d/cmdline", pid, tid);
    read_cmdline(filename, proc);
  }
  return proc;
}

static void read_smaps(pid_t pid, struct proc_info *p)
{
  FILE *fp;
  char *line;
  size_t len;
  long long start, end, val, prvcl, prvdr, shrdr, shrcl;
  int count;

  p->vss = p->rss = 0;
  start = end = val = prvcl = prvdr = shrdr = shrcl = 0;
  sprintf(toybuf, "/proc/%u/smaps", pid);
  if (!(fp = fopen(toybuf, "r"))) {
    error_msg("No %ld\n", (long)pid);
    return;
  }
  for (;;) {
    int off;

    line = 0;
    if (0 >= getline(&line, &len, fp)) break;
    count = sscanf(line, "%llx-%llx %s %*s %*s %*s %n",
        &start, &end, toybuf, &off);

    if (count == 3) {
      end = end - start;
      if (strncmp(line+off, "/dev/", 5) || !strcmp(line+off, "/dev/zero\n")) {
        p->vss += end;
        if (toybuf[1] == 'w') p->vssrw += end;
      }
      if (line[off] && !strncmp(line+off, "[stack]",7)) p->stack += end;
    } else {
      if (0<sscanf(line, "Private_Clean: %lld", &val)) prvcl += val;
      if (0<sscanf(line, "Private_Dirty: %lld", &val)) prvdr += val;
      if (0<sscanf(line, "Shared_Dirty: %lld", &val)) shrdr += val;
      if (0<sscanf(line, "Shared_Clean: %lld", &val)) shrcl += val;
    }
    free(line);
  }
  free(line); //incase it broke out.
  p->rss_shr = shrdr + shrcl;
  p->drt = prvdr + shrdr;
  p->drt_shr = shrdr;
  p->rss = p->rss_shr + prvdr + prvcl;
  fclose(fp);
}

static void read_procs(void) // Read Processes
{
  DIR *proc_dir, *thr_dir;
  struct dirent *pid_dir, *t_dir;
  struct proc_info *proc;
  pid_t pid, tid;
  int proc_num = 0;

  proc_dir = opendir("/proc");
  if (!proc_dir) perror_exit("Could not open /proc");

  new_procs = xzalloc(INIT_PROCS * sizeof(struct proc_info *));
  TT.num_new_procs = INIT_PROCS;

  while ((pid_dir = readdir(proc_dir))) {
    if (!isdigit(pid_dir->d_name[0])) continue;

    pid = atoi(pid_dir->d_name);
    proc = add_new_proc(pid, 0);
    if (TT.m_flag) {
      read_smaps(pid, proc);
      if (!proc->vss) {
        free(proc);
        continue;
      }
    }
    add_proc(proc_num++, proc);

    if (TT.threads) {
      char filename[64];
      uid_t uid = proc->uid;

      sprintf(filename,"/proc/%d/task",pid);
      if ((thr_dir = opendir(filename))) {
        while ((t_dir = readdir(thr_dir))) {
          if (!isdigit(t_dir->d_name[0])) continue;   

          tid = atoi(t_dir->d_name);
          if (pid == tid) continue;
          proc = add_new_proc(pid, tid);
          proc->uid = uid; //child will have same uid as parent.
          add_proc(proc_num++, proc);
        }
        closedir(thr_dir);
      }
    }
  }

  closedir(proc_dir);
  TT.num_new_procs = proc_num;
}

//calculate percentage.
static char* show_percent(long unsigned num, long unsigned den)
{
  long res;
  static char ch, buff[12]={'\0'};

  if(num > den) num = den;
  res = (num * 100)/den;
  sprintf(buff,"%ld", (num * 100)% den);
  ch = *buff;
  sprintf(buff, "%ld.%c",res, ch);
  return buff;
}

static int print_header(struct sysinfo *info, unsigned int cols)
{
  int fd, j, k, rows =0;
  long unsigned total, meminfo_cached, anon, meminfo_mapped,
       meminfo_slab, meminfo_dirty, meminfo_writeback, swapT, swapF;
  char *buff;

  fd = xopen("/proc/meminfo", O_RDONLY);
  while ((buff = get_line(fd))) {
    if (!strncmp(buff, "Cached", 6))
      sscanf(buff,"%*s %lu\n",&meminfo_cached);
    else if (!strncmp(buff, "AnonPages", 9))
      sscanf(buff,"%*s %lu\n",&anon);
    else if (!strncmp(buff, "Mapped", 6))
      sscanf(buff,"%*s %lu\n",&meminfo_mapped);
    else if (!strncmp(buff, "Slab", 4))
      sscanf(buff,"%*s %lu\n",&meminfo_slab);
    else if (!strncmp(buff, "Dirty", 5))
      sscanf(buff,"%*s %lu\n",&meminfo_dirty);
    else if (!strncmp(buff, "Writeback", 9))
      sscanf(buff,"%*s %lu\n",&meminfo_writeback);
    else if (!strncmp(buff, "SwapTotal", 9))
      sscanf(buff,"%*s %lu\n",&swapT);
    else if (!strncmp(buff, "SwapFree", 8))
      sscanf(buff,"%*s %lu\n",&swapF);
    free(buff);
  }
  close(fd);

  if (!(toys.optflags & FLAG_b)) printf("\033[H\033[J");

  if (TT.m_flag){
    sprintf(toybuf, "Mem total:%lu anon:%lu map:%lu free:%lu", 
        ((info->totalram) >> 10), anon, meminfo_mapped,
        ((info->freeram) >> 10));
    printf("%.*s\n", cols, toybuf);

    sprintf(toybuf, "slab:%lu buf:%lu cache:%lu dirty:%lu write:%lu",
        meminfo_slab, ((info->bufferram) >>10), meminfo_cached,
        meminfo_dirty,meminfo_writeback);
    printf("%.*s\n", cols, toybuf);

    sprintf(toybuf, "Swap total:%lu free:%lu",swapT, swapF);
    printf("%.*s\n", cols, toybuf);
    rows += 3;
  } else {
    sprintf(toybuf,"Mem: %luK used, %luK free, %luK shrd, %luK buff, %luK cached", 
        (info->totalram-info->freeram) >>10, (info->freeram) >>10, 
        (info->sharedram) >>10, (info->bufferram) >>10, meminfo_cached);
    printf("%.*s\n", cols, toybuf);

    for (k = 1; new_cpu[k].total; k++) {
      j = 0;
      if (!TT.smp) { 
        k = 0;
        j = sprintf(toybuf,"CPU:");
      } else j = sprintf(toybuf,"CPU%d:", k-1);

      total = (new_cpu[k].total) - (old_cpu[k].total);
      if (!total) total = 1; //avoid denominator as 0, FPE
      j += sprintf(toybuf + j," %s%% usr", 
          show_percent((new_cpu[k].utime - old_cpu[k].utime), total));
      j += sprintf(toybuf+j," %s%% sys",
          show_percent((new_cpu[k].stime - old_cpu[k].stime), total));
      j += sprintf(toybuf+j," %s%% nic",
          show_percent(new_cpu[k].ntime - old_cpu[k].ntime, total));
      j += sprintf(toybuf+j," %s%% idle",
          show_percent(new_cpu[k].itime - old_cpu[k].itime, total));
      j += sprintf(toybuf+j," %s%% io",
          show_percent((new_cpu[k].iowtime - old_cpu[k].iowtime), total));
      j += sprintf(toybuf+j," %s%% irq",
          show_percent(new_cpu[k].irqtime - old_cpu[k].irqtime, total));
      j += sprintf(toybuf+j," %s%% sirq",
          show_percent(new_cpu[k].sirqtime - old_cpu[k].sirqtime, total));
      printf("%.*s\n", cols, toybuf);
      if (!TT.smp) break;
    }

    if ((buff = readfile("/proc/loadavg", NULL, 0))) {
      buff[strlen(buff) -1] = '\0'; //removing '\n' at end
      sprintf(toybuf, "Load average: %s", buff);
      printf("%.*s\n", cols, toybuf);
      free(buff);
    }
    rows += 2 + ((TT.smp) ? k-1 : 1);
  }
  return rows;
}

static void print_procs(void) 
{
  int i, j = 0;
  struct proc_info *old_proc, *proc;
  long unsigned total_delta_time;
  struct passwd *user;
  char *user_str, user_buf[20];
  struct sysinfo info;
  unsigned int cols=0, rows =0;

  terminal_size(&cols, &rows);
  if (!rows){
    rows = 24; //on serial consoles setting default
    cols = 79;
  }
  if (toys.optflags & FLAG_b) rows = INT_MAX;
  TT.rows = rows;

  for (i = 0; i < TT.num_new_procs; i++) {
    if (new_procs[i]) {
      old_proc = find_old_proc(new_procs[i]->pid);
      if (old_proc) {
        new_procs[i]->delta_utime = new_procs[i]->utime - old_proc->utime;
        new_procs[i]->delta_stime = new_procs[i]->stime - old_proc->stime;
      } else {
        new_procs[i]->delta_utime = 0;
        new_procs[i]->delta_stime = 0;
      }
      new_procs[i]->delta_time = new_procs[i]->delta_utime 
        + new_procs[i]->delta_stime;
    }
  }

  total_delta_time = new_cpu[0].total - old_cpu[0].total;
  if (!total_delta_time) total_delta_time = 1;

  qsort(new_procs, TT.num_new_procs, sizeof(struct proc_info *), proc_cmp);

  //Memory details
  sysinfo(&info);
  info.totalram *= info.mem_unit;
  info.freeram *= info.mem_unit;
  info.sharedram *= info.mem_unit;
  info.bufferram *= info.mem_unit;

  rows -= print_header(&info, cols);
 
  if (TT.m_flag) {
    sprintf(toybuf, "%5s %5s %5s %5s %5s %5s %5s %5s %s", "PID", "VSZ", "VSZRW",
        "RSS", "(SHR)", "DIRTY", "(SHR)", "STACK", "COMMAND");
    toybuf[11 + TT.cmp_field*6] = (TT.reverse)?'_':'^'; //11 for PID,VSZ fields
  } else sprintf(toybuf, "%5s %5s %-8s %4s %5s %5s %4s %5s %s", "PID", "PPID", 
      "USER", "STAT", "VSZ", "%VSZ", "CPU" , "%CPU", "COMMAND");

  printf((toys.optflags & FLAG_b)?"%.*s\n":"\033[7m%.*s\033[0m\n",cols, toybuf);
  rows--;
  for (i = TT.scroll_offset; i < TT.num_new_procs; i++) {
    j = 0;
    proc = new_procs[i];

    user  = getpwuid(proc->uid);
    if (user && user->pw_name) {
      user_str = user->pw_name;
    } else {
      snprintf(user_buf, 20, "%d", proc->uid);
      user_str = user_buf;
    }

    if (!TT.m_flag )
    {
      float vss_percentage = (float)(proc->vss)/info.totalram * 100;

      j = sprintf(toybuf, "%5d %5d %-8.8s %-4s",proc->pid, proc->ppid, user_str,
          proc->state);

      if ((proc->vss >> 10) >= 100000) 
        j += sprintf(toybuf + j, " %4lum", ((proc->vss >> 10) >> 10));
      else j += sprintf(toybuf+j, " %5lu", (proc->vss >> 10));

      sprintf(toybuf + j," %5.1f %4d %5s %s", vss_percentage, proc->prs, 
          show_percent(proc->delta_time, total_delta_time), 
          ((proc->name[0])? proc->name : proc->tname));
      printf("%.*s", cols, toybuf);
    } else {
      j = sprintf(toybuf, "%5d",proc->pid);

      if ((proc->vss >> 10) >= 100000) 
        j += sprintf(toybuf + j, " %4lum", ((proc->vss >> 10) >> 10));
      else j += sprintf(toybuf+j, " %5lu", (proc->vss >> 10));
      if ((proc->vssrw >>10) >= 100000)
        j += sprintf(toybuf + j, " %4lum", ((proc->vssrw >> 10) >> 10));
      else j += sprintf(toybuf+j, " %5lu", (proc->vssrw >> 10)); 
      if (proc->rss >= 100000)
        j += sprintf(toybuf + j, " %4lum", ((proc->rss >> 10)));
      else j += sprintf(toybuf+j, " %5lu", proc->rss);
      if (proc->rss_shr >= 100000)
        j += sprintf(toybuf + j, " %4lum", (proc->rss_shr >> 10));
      else j += sprintf(toybuf+j, " %5lu", proc->rss_shr);
      if (proc->drt >= 100000)
        j += sprintf(toybuf + j, " %4lum", (proc->drt >> 10));
      else j += sprintf(toybuf+j, " %5lu", proc->drt);
      if (proc->drt_shr >= 100000)
        j += sprintf(toybuf + j, " %4lum", (proc->drt_shr >> 10));
      else j += sprintf(toybuf+j, " %5lu", proc->drt_shr);
      if ((proc->stack >>10) >= 100000)
        j += sprintf(toybuf + j, " %4lum", ((proc->stack >> 10) >> 10));
      else j += sprintf(toybuf+j, " %5lu", (proc->stack >> 10));
      
      sprintf(toybuf + j," %s",((proc->name[0])? proc->name : proc->tname));
      printf("%.*s", cols, toybuf);
    }
    rows--;
    if (!rows) {
      xputc('\r');
      break; //don't print any more process details.
    } else xputc('\n');
  }
}

/* 
 * Free old processes(displayed in old iteration) in order to 
 * avoid memory leaks
 */
static void free_procs_arr(struct proc_info **procs) 
{
  int i;
  for (i = 0; procs && procs[i]; i++)
      free_proc(procs[i]);

  free(procs);
}

static int numcmp(long long a, long long b) 
{
  if (a < b) return (TT.reverse)?-1 : 1;
  if (a > b) return (TT.reverse)?1 : -1;
  return 0;
}

static int top_mem_cmp(const void *a, const void *b)
{
  char *pa, *pb;

  int n = offsetof(struct proc_info, vss) + TT.cmp_field * sizeof(unsigned long);
  pa = *((char **)a); pb = *((char **)b);
  return numcmp(*(unsigned long*)(pa+n), *(unsigned long*)(pb+n));
}

static int proc_time_cmp(const void *a, const void *b) 
{
  struct proc_info *pa, *pb;

  pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);
  return numcmp(pa->utime + pa->stime, pb->utime+pa->stime);
}

/*
 * Function to compare CPU usgae % while displaying processes 
 * according to CPU usage
 */
static int proc_cpu_cmp(const void *a, const void *b) 
{
  struct proc_info *pa, *pb;

  pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);
  return numcmp(pa->delta_time, pb->delta_time);
}

/*
 * Function to compare memory taking by a process at the time of 
 * displaying processes according to Memory usage
 */ 
static int proc_vss_cmp(const void *a, const void *b) 
{
  struct proc_info *pa, *pb;

  pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);
  return numcmp(pa->vss, pb->vss);
}

static int proc_pid_cmp(const void *a, const void *b) 
{
  struct proc_info *pa, *pb;

  pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);
  return numcmp(pa->pid, pb->pid);
}

/* Read CPU stats for all the cores, assuming max 8 cores
 * to be present here.
 */
static void read_cpu_stat()
{
  int i;
  size_t len;
  char *line = 0, *params = "%lu %lu %lu %lu %lu %lu %lu %lu";
  FILE *fp = xfopen("/proc/stat", "r");

  for (i = 0; i<=8 && getline(&line, &len, fp) > 0; i++) {
    if (i) sprintf(toybuf, "cpu%d %s", i-1, params);
    else sprintf(toybuf, "cpu  %s",  params);
    len = sscanf(line, toybuf, &new_cpu[i].utime, &new_cpu[i].ntime, 
        &new_cpu[i].stime, &new_cpu[i].itime, &new_cpu[i].iowtime, 
        &new_cpu[i].irqtime, &new_cpu[i].sirqtime, &new_cpu[i].steal);
    if (len == 8) 
      new_cpu[i].total = new_cpu[i].utime + new_cpu[i].ntime + new_cpu[i].stime
      + new_cpu[i].itime + new_cpu[i].iowtime + new_cpu[i].irqtime
      + new_cpu[i].sirqtime + new_cpu[i].steal;

    free(line);
    line = 0;
  }
  fclose(fp);
}

void top_main(void )
{
  int get_key;

  proc_cmp = &proc_cpu_cmp;
  if ( TT.delay < 0)  TT.delay = 3;
  if (toys.optflags & FLAG_m) {
    proc_cmp = &top_mem_cmp;
    TT.m_flag = 1;
  }

  sigatexit(signal_handler);
  read_cpu_stat();
  get_key = read_input(0);

  while (!(toys.optflags & FLAG_n) || TT.iterations--) {
    old_procs = new_procs;
    memcpy(old_cpu, new_cpu, sizeof(old_cpu));
    read_procs();
    read_cpu_stat();
    print_procs();
    free_procs_arr(old_procs);
    if ((toys.optflags & FLAG_n) && !TT.iterations) break;

    get_key = read_input(TT.delay);
    if (get_key == 'q') break;

    switch(get_key) {
      case 'n':
        proc_cmp = &proc_pid_cmp;
        TT.m_flag = 0;
        break;
      case 'h':
        if (!TT.m_flag) TT.threads ^= 1;
        break;
      case 'm':
        proc_cmp = &proc_vss_cmp;
        TT.m_flag = 0;
        break;
      case 'r':
        TT.reverse ^= 1;
        break;
      case 'c':
      case '1':
        TT.smp ^= 1;
        break;
      case 's':
        TT.m_flag = 1;
        TT.cmp_field = (TT.cmp_field + 1) % 7;//7 sort fields, vss,vssrw...
        proc_cmp = &top_mem_cmp;
        break;
      case 'p':
        proc_cmp = &proc_cpu_cmp;
        TT.m_flag = 0;
        break;
      case 't':
        proc_cmp = &proc_time_cmp;
        TT.m_flag = 0;
        break;
      case KEY_UP:
        TT.scroll_offset--;
        break;
      case KEY_DOWN:
        TT.scroll_offset++;
        break;
      case KEY_HOME:
        TT.scroll_offset = 0;
        break;
      case  KEY_END:
        TT.scroll_offset = TT.num_new_procs - TT.rows/2;
        break;
      case KEY_PAGEUP:
        TT.scroll_offset -= TT.rows/2;
        break;
      case KEY_PAGEDN: 
        TT.scroll_offset += TT.rows/2;
        break;
    }
    if (TT.scroll_offset >= TT.num_new_procs) TT.scroll_offset = TT.num_new_procs-1;
    if (TT.scroll_offset < 0) TT.scroll_offset = 0;
  }
  xputc('\n');
  if (CFG_TOYBOX_FREE) {
    free_proc_list(free_procs);
    free_procs = NULL;
    free_procs_arr(new_procs);
    free_proc_list(free_procs);
  }
}
