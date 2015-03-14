/* ps.c - Show running process statistics.
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html

USE_PS(NEWTOY(ps, ">0o*T", TOYFLAG_BIN))

config PS
  bool "ps"
  default n
  help
    usage: ps [-o COL1,COL2=HEADER] [-T]
    
    Show list of processes

    -a	Show all processes with a tty
    -A  Show all processes
    -o	Select columns for display
    -T	Show threads
*/

#define FOR_ps
#include "toys.h"

GLOBALS(
  struct arg_list *llist_o;
  unsigned screen_width;

  void *o;
)

#define BUFF_SIZE 1024
struct header_list {
  struct header_list *next;
  char *name, *header, *format;
  int width, position;
};

// create list of header attributes taking care of -o (-o ooid=MOM..)
// and width of attributes.
static void list_add(struct header_list *data, char *c_data)
{
  struct header_list *temp = TT.o, *new = xzalloc(sizeof(struct header_list));

  new->name = data->name;   
  if (c_data) new->header = c_data;
  else new->header = xstrdup(data->header);  
  if (c_data && (strlen(c_data) > data->width)) new->width = strlen(c_data);
  else new->width = data->width;
  new->format = data->format;
  new->position = data->position;

  if (temp) {
    while (temp->next) temp = temp->next;
    temp->next = new;
  } else TT.o = new;
}

//print the default header OR header with -o args
static void print_header(struct header_list *hdr, int hdr_len)
{
  int i = 0;
  char *ptr = NULL, *str, *temp;
  struct arg_list *node = TT.llist_o;

  // Default pid, user, time, comm
  if (!node) {
    list_add(hdr+4, 0);
    list_add(hdr, 0);
    list_add(hdr+11, 0);
    list_add(hdr+3, 0);
  }

  while (node) {
    char *s = str = xstrdup(node->arg);

    i = 0;
    while (str) {
      if ((ptr = strsep(&str, ","))) { //seprate list
        if ((temp = strchr(ptr, '='))) { // Handle ppid = MOM
          *temp = 0;
          temp++;
          while (hdr[i].name) {
            // search from default header
            if (!(strcmp(hdr[i].name, ptr))) {
              //handle condition like ppid = M,OM
              if (str) ptr = xmprintf("%s,%s", temp, str);
              else ptr = xmprintf("%s", temp);
              list_add(hdr+i, ptr);
              break;
            }
            i++; 
          }
          if (!hdr[i].name) perror_exit("Invalid arg for -o option");
          break;
        } else {
          while (hdr[i].name) {
            if (!(strcmp(hdr[i].name, ptr))) {
              list_add(hdr+i, 0);
              break;
            }
            i++; 
          }
          if (!hdr[i].name) error_exit("bad -o");
          i = 0;
        }
      }
    }
    free(s);
    node = node->next;
  }

  for (hdr = TT.o; hdr; hdr = hdr->next)
    printf(hdr->format , hdr->width, hdr->header);
  xputc('\n');
}

//get uid/gid for processes.
static void get_uid_gid(char *p, char *id_str, unsigned *id)
{
  FILE *f; 

  if(!p) return;
  f = xfopen(p, "r");
  while (fgets(toybuf, BUFF_SIZE, f)) {
    if (!strncmp(toybuf, id_str, strlen(id_str))) {
      sscanf(toybuf, "%*s %u", id);
      break;
    }        
  }
  fclose(f);
}

//get etime for processes.
void get_etime(unsigned long s_time)
{
  unsigned long min;
  unsigned sec;
  struct sysinfo info;
  char *temp;

  sysinfo(&info);
  min = s_time/sysconf(_SC_CLK_TCK);
  min = info.uptime - min;
  sec = min % 60;
  min = min / 60;
  temp = xmprintf("%3lu:%02u", min,sec);
  xprintf("%*.*s",7,7,temp);
  free(temp);
}

//get time attributes for processes.
void get_time(unsigned long s_time, unsigned long u_time)
{        
  unsigned long min;
  unsigned sec;
  char *temp;

  min = (s_time + u_time)/sysconf(_SC_CLK_TCK);
  sec = min % 60;
  min = min / 60;
  temp = xmprintf("%3lu:%02u", min,sec);
  xprintf("%*.*s",6,6,temp);
  free(temp);
}

// read command line taking care of in between NUL's in command line
static void read_cmdline(int fd, char *cmd_ptr)
{
  int size = read(fd, cmd_ptr, BUFF_SIZE); //sizeof(cmd_buf)

  cmd_ptr[size] = '\0';
  while (--size > 0 && cmd_ptr[size] == '\0'); //reach to last char

  while (size >= 0) {
    if ((unsigned char)cmd_ptr[size] < ' ') cmd_ptr[size] = ' ';
    size--;
  }
}

// get the processes stats and print the stats
// corresponding to header attributes.
static void do_ps_line(int pid, int tid)
{
  char *stat_buff = toybuf + BUFF_SIZE, *cmd_buff = toybuf + (2*BUFF_SIZE);
  char state[4] = {0,};
  int tty, tty_major, tty_minor, fd, n, nice, width_counter = 0;
  struct stat stats;
  struct passwd *pw;
  struct group *gr;
  char *name, *user, *group, *ruser, *rgroup, *ptr;
  long rss;
  unsigned long stime, utime, start_time, vsz;
  unsigned ppid, ruid, rgid, pgid;
  struct header_list *p = TT.o;

  sprintf(stat_buff, "/proc/%d", pid);
  if(stat(stat_buff, &stats)) return;

  if (tid) {
    if (snprintf(stat_buff, BUFF_SIZE, "/proc/%d/task/%d/stat", pid, tid) >= BUFF_SIZE) return;
    if (snprintf(cmd_buff, BUFF_SIZE, "/proc/%d/task/%d/cmdline", pid, tid) >= BUFF_SIZE) return;
  } else {
    if (snprintf(stat_buff, BUFF_SIZE, "/proc/%d/stat", pid) >= BUFF_SIZE) return;
    if (snprintf(cmd_buff, BUFF_SIZE, "/proc/%d/cmdline", pid) >= BUFF_SIZE) return;
  }

  fd = xopen(stat_buff, O_RDONLY);
  n = readall(fd, stat_buff, BUFF_SIZE);
  xclose(fd);
  if (n < 0) return;
  stat_buff[n] = 0; //Null terminate the buffer.
  ptr = strchr(stat_buff, '(');
  ptr++;
  name = ptr;
  ptr = strrchr(stat_buff, ')');
  *ptr = '\0'; //unecessary if?
  name = xmprintf("[%s]", name);
  ptr += 2; // goto STATE
  n = sscanf(ptr, "%c %u %u %*u %d %*s %*s %*s %*s %*s %*s "
      "%lu %lu %*s %*s %*s %d %*s %*s %lu %lu %ld",            
      &state[0],&ppid, &pgid, &tty, &utime, &stime,
      &nice,&start_time, &vsz,&rss);

  if (tid) pid = tid;
  vsz >>= 10; //Convert into KB
  rss = rss * 4; //Express in pages
  tty_major = (tty >> 8) & 0xfff;
  tty_minor = (tty & 0xff) | ((tty >> 12) & 0xfff00);

  if (vsz == 0 && state[0] != 'Z') state[1] = 'W';
  else state[1] = ' ';
  if (nice < 0 ) state[2] = '<';
  else if (nice) state[2] = 'N';
  else state[2] = ' ';

  if (tid) {
    if (snprintf(stat_buff, BUFF_SIZE, "/proc/%d/task/%d/status", pid, tid) >= BUFF_SIZE)
      goto clean;
  } else {
    if (snprintf(stat_buff, BUFF_SIZE, "/proc/%d/status", pid) >= BUFF_SIZE)
      goto clean;
  }

  fd = -1;
  while (p) {
    int width;
    width = p->width;
    width_counter += (width + 1); //how much screen we hv filled, +1, extra space b/w headers
    switch (p->position) {
      case 0:
        pw = getpwuid(stats.st_uid);
        if (!pw) user = xmprintf("%d",(int)stats.st_uid);
        else user = xmprintf("%s", pw->pw_name);
        printf("%-*.*s", width, width, user);
        free(user);
        break;
      case 1:
        gr = getgrgid(stats.st_gid);
        if (!gr) group = xmprintf("%d",(int)stats.st_gid);
        else group = xmprintf("%s", gr->gr_name);
        printf("%-*.*s", width, width, group);
        free(group);
        break;
      case 2:
        name[strlen(name) - 1] = '\0';
        printf("%-*.*s", width,width, name + 1);
        name[strlen(name)] = ']'; //Refill it for further process.
        break;
      case 3:
        {
          int j = 0;
          width_counter -= width;
          if(p->next) j = width; //is args is in middle. ( -o pid,args,ppid)
          else j = (TT.screen_width - width_counter % TT.screen_width); //how much screen left.
          if (fd == -1) fd = open(cmd_buff, O_RDONLY); //don't want to die
          else xlseek(fd, 0, SEEK_SET);
          if (fd < 0) cmd_buff[0] = 0;
          else read_cmdline(fd, cmd_buff); 
          if (cmd_buff[0]) printf("%-*.*s", j, j, cmd_buff);
          else printf("%-*.*s", j, j, name);
          width_counter += width;
          break;
        }
      case 4:
        printf("%*d", width, pid);
        break;
      case 5:
        printf("%*d", width, ppid);
        break;
      case 6:
        printf("%*d", width, pgid);
        break;
      case 7:
        get_etime(start_time);
        break;
      case 8:
        printf("%*d", width, nice);
        break;
      case 9:
        get_uid_gid(stat_buff, "Gid:", &rgid);
        gr = getgrgid(rgid);
        if (!gr) rgroup = xmprintf("%d",(int)stats.st_gid);
        else rgroup = xmprintf("%s", gr->gr_name);
        printf("%-*.*s", width,width, rgroup);
        free(rgroup);
        break;
      case 10:
        get_uid_gid(stat_buff, "Uid:", &ruid);
        pw = getpwuid(ruid);
        if (!pw) ruser = xmprintf("%d",(int)stats.st_uid);
        else ruser = xmprintf("%s", pw->pw_name);
        printf("%-*.*s", width, width, ruser);
        free(ruser);
        break;
      case 11:
        get_time(utime, stime);
        break;
      case 12:
        if (tty_major) {
          char *temp = xmprintf("%d,%d", tty_major,tty_minor);
          printf("%-*s", width, temp);
          free(temp);
        } else printf("%-*s", width, "?");
        break;
      case 13:
        printf("%*lu", width, vsz);
        break;
      case 14:
        printf("%-*s", width, state);
        break;
      case 15:
        printf("%*lu", width, rss);
        break;
    }
    p = p->next;
    xputc(' '); //space char
  }
  if (fd >= 0) xclose(fd);
  xputc('\n');
clean:
  free(name);
}

// Do stats for threads (for -T option)
void do_ps_threads(int pid)
{       
  DIR *d; 
  int tid;
  struct dirent *de;
  char *tmp = xmprintf("/proc/%d/task",pid);

  if (!(d = opendir(tmp))) {
    free(tmp);
    return;
  }
  while ((de = readdir(d))) {
    if (isdigit(de->d_name[0])) {
      tid = atoi(de->d_name);
      if (tid == pid) continue;
      do_ps_line(pid, tid);
    }
  }        
  closedir(d); 
  free(tmp);
}

void ps_main(void)
{
  DIR *dp;
  struct dirent *entry;
  int pid;
  struct header_list def_header[] = { 
    {0, "user", "USER", "%-*s ", 8, 0},
    {0, "group", "GROUP", "%-*s ", 8, 1},
    {0, "comm", "COMMAND", "%-*s ",16, 2},
    {0, "args", "COMMAND", "%-*s ",30, 3},
    {0, "pid", "PID", "%*s ", 5, 4},
    {0, "ppid","PPID", "%*s ", 5, 5},
    {0, "pgid", "PGID", "%*s ", 5, 6},
    {0, "etime","ELAPSED", "%*s ", 7, 7},
    {0, "nice", "NI", "%*s ", 5, 8},
    {0, "rgroup","RGROUP", "%-*s ", 8, 9},
    {0, "ruser","RUSER", "%-*s ", 8, 10},
    {0, "time", "TIME", "%*s ", 6, 11},
    {0, "tty", "TT", "%-*s ", 6, 12},
    {0, "vsz","VSZ", "%*s ", 7, 13},
    {0, "stat", "STAT", "%-*s ", 4, 14},
    {0, "rss", "RSS", "%*s ", 4, 15},
{0,0,0,0,0,0}
  };
  
  TT.screen_width = 80; //default width
  terminal_size(&TT.screen_width, NULL);
  print_header(def_header, ARRAY_LEN(def_header));

  if (!(dp = opendir("/proc"))) perror_exit("opendir");
  while ((entry = readdir(dp))) {
    if (!isdigit(*entry->d_name)) continue;
    pid = atoi(entry->d_name);
    do_ps_line(pid, 0);
    if (toys.optflags & FLAG_T) do_ps_threads(pid);
  }
  closedir(dp);

  while (CFG_TOYBOX_FREE) {
    struct header_list *temp = llist_pop(&TT.o);

    if (!temp) break;
    free(temp->header);
    free(temp);
  }
}
