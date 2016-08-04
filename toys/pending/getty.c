/* getty.c - A getty program to get controlling terminal.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gamil.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_GETTY(NEWTOY(getty, "<2t#<0H:I:l:f:iwnmLh",TOYFLAG_SBIN))

config GETTY
  bool "getty"
  default n
  help
    usage: getty [OPTIONS] BAUD_RATE[,BAUD_RATE]... TTY [TERMTYPE]

    -h    Enable hardware RTS/CTS flow control
    -L    Set CLOCAL (ignore Carrier Detect state)
    -m    Get baud rate from modem's CONNECT status message
    -n    Don't prompt for login name
    -w    Wait for CR or LF before sending /etc/issue
    -i    Don't display /etc/issue
    -f ISSUE_FILE  Display ISSUE_FILE instead of /etc/issue
    -l LOGIN  Invoke LOGIN instead of /bin/login
    -t SEC    Terminate after SEC if no login name is read
    -I INITSTR  Send INITSTR before anything else
    -H HOST    Log HOST into the utmp file as the hostname
*/
#define FOR_getty
#include "toys.h"
#include <utmp.h>

GLOBALS(
  char *issue_str;
  char *login_str;
  char *init_str;
  char *host_str; 
  long timeout;
  
  char *tty_name;  
  int  speeds[20];
  int  sc;              
  struct termios termios;
  char buff[128];
)

#define CTL(x)        ((x) ^ 0100) 
#define HOSTNAME_SIZE 32

typedef void (*sighandler_t)(int);
struct speed_mapper {
  long speed;
  speed_t code;
};

struct speed_mapper speedtab[] = {
  {50, B50}, {75, B75}, {110, B110}, {134, B134}, {150, B150}, {200, B200},
  {300, B300}, {600, B600}, {1200, B1200}, {1800, B1800}, {2400, B2400},
  {4800, B4800}, {9600, B9600},
#ifdef  B19200
  {19200, B19200},
#endif
#ifdef  B38400
  {38400, B38400},
#endif
#ifdef  EXTA
  {19200, EXTA},
#endif
#ifdef  EXTB
  {38400, B38400},
#endif
#ifdef B57600
  {57600, B57600},
#endif
#ifdef B115200
  {115200, B115200},
#endif
#ifdef B230400
  {230400, B230400},
#endif
  {0, 0},
};

// Find speed from mapper array 
static speed_t encode(char *s)
{
  struct speed_mapper *sp;
  long speed = atolx(s);

  if (!speed) return 0;
  for (sp = speedtab; sp->speed; sp++) if (sp->speed == speed) return sp->code;
  return (speed_t) -1;
}

static void get_speed(char *sp)
{
  char *ptr;

  TT.sc = 0;
  while ((ptr = strsep(&sp, ","))) {
    TT.speeds[TT.sc] = encode(ptr);
    if (TT.speeds[TT.sc] < 0) perror_exit("bad speed");
    if (++TT.sc > 10) perror_exit("too many speeds, max is 10");
  }
}

// Parse args and set TERM env. variable
static void parse_arguments(void)
{
  if (isdigit(**toys.optargs)) {
    get_speed(*toys.optargs);
    if (*++toys.optargs) TT.tty_name = xmprintf("%s", *toys.optargs);
  } else {
    TT.tty_name = xmprintf("%s", *toys.optargs);
    if (*++toys.optargs) get_speed(*toys.optargs);
  } 
  if (*++toys.optargs) setenv("TERM", *toys.optargs, 1);
}

// Get controlling terminal and redirect stdio 
static void open_tty(void)
{
  if (strcmp(TT.tty_name, "-")) {
    if (*(TT.tty_name) != '/') TT.tty_name = xmprintf("/dev/%s", TT.tty_name);
    // Sends SIGHUP to all foreground process if Session leader don't die,Ignore
    sighandler_t sig = signal(SIGHUP, SIG_IGN); 
    ioctl(0, TIOCNOTTY, 0); // Giveup if there is any controlling terminal
    signal(SIGHUP, sig);
    if ((setsid() < 0) && (getpid() != getsid(0))) 
      perror_exit("setsid");
    xclose(0);
    xopen_stdio(TT.tty_name, O_RDWR|O_NDELAY|O_CLOEXEC);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK); // Block read
    dup2(0, 1);
    dup2(0, 2);
    if (ioctl(0, TIOCSCTTY, 1) < 0) perror_msg("ioctl(TIOCSCTTY)");
    if (!isatty(0)) perror_exit("/dev/%s: not a tty", TT.tty_name);
    chown(TT.tty_name, 0, 0); // change ownership, Hope login will change this
    chmod(TT.tty_name, 0620);
  } else { // We already have opened TTY
    if (setsid() < 0) perror_msg("setsid failed");
    if ((fcntl(0, F_GETFL) & (O_RDWR|O_RDONLY|O_WRONLY)) != O_RDWR)
      perror_exit("no read/write permission");
  }
}

// Intialise terminal settings
static void termios_init(void)
{
  if (tcgetattr(STDIN_FILENO, &TT.termios) < 0) perror_exit("tcgetattr");
  // Flush input and output queues, important for modems!
  tcflush(STDIN_FILENO, TCIOFLUSH); 
  TT.termios.c_cflag &= (0|CSTOPB|PARENB|PARODD);
#ifdef CRTSCTS
  if (toys.optflags & FLAG_h) TT.termios.c_cflag |= CRTSCTS;
#endif
  if (toys.optflags & FLAG_L) TT.termios.c_cflag |= CLOCAL;
  TT.termios.c_cc[VTIME] = 0;
  TT.termios.c_cc[VMIN] = 1;
  TT.termios.c_oflag = OPOST|ONLCR;
  TT.termios.c_cflag |= CS8|CREAD|HUPCL|CBAUDEX;
  // login will disable echo for passwd.
  TT.termios.c_lflag |= ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHOKE;
  TT.termios.c_cc[VINTR] = CTL('C');
  TT.termios.c_cc[VQUIT] = CTL('\\');
  TT.termios.c_cc[VEOF] = CTL('D');
  TT.termios.c_cc[VEOL] = '\n';
  TT.termios.c_cc[VKILL] = CTL('U');
  TT.termios.c_cc[VERASE] = 127; // CERASE
  TT.termios.c_iflag = ICRNL|IXON|IXOFF;
  // set non-zero baud rate. Zero baud rate left it unchanged.
  if (TT.speeds[0] != B0) cfsetspeed(&TT.termios, TT.speeds[0]); 
  if (tcsetattr(STDIN_FILENO, TCSANOW, &TT.termios) < 0) 
    perror_exit("tcsetattr");
}

// Get the baud rate from modems CONNECT mesage, Its of form <junk><BAUD><Junk>
static void sense_baud(void)
{
  int vmin;
  ssize_t size;
  char *ptr;
  speed_t speed;

  vmin = TT.termios.c_cc[VMIN]; // Store old
  TT.termios.c_cc[VMIN] = 0; // No block even queue is empty.
  if (tcsetattr(STDIN_FILENO, TCSANOW, &TT.termios) < 0) 
    perror_exit("tcsetattr");
  size = readall(STDIN_FILENO, TT.buff, sizeof(TT.buff)-1);
  if (size > 0) {
    for (ptr = TT.buff; ptr < TT.buff+size; ptr++) {
      if (isdigit(*ptr)) {
        speed = encode(ptr);
        if (speed > 0) cfsetspeed(&TT.termios,speed);
        break;
      }
    } 
  }
  TT.termios.c_cc[VMIN] = vmin; //restore old value
  if (tcsetattr(STDIN_FILENO, TCSANOW, &TT.termios) < 0)
    perror_exit("tcsetattr");
}

// Just prompt for login name 
void print_prompt(void)
{
  char *hostname;
  struct utsname uts;

  uname(&uts);
  hostname = xstrdup(uts.nodename);
  fputs(hostname, stdout);
  fputs(" login: ", stdout);
  fflush(NULL);
  free(hostname);
  hostname = NULL;
}

// Print /etc/isuue with taking care of each escape sequence
void write_issue(char *file)
{
  char buff[20] = {0,};
  struct utsname u;
  uname(&u);
  int size, fd = open(TT.issue_str, O_RDONLY);

  if (fd < 0) return;
  while ((size = readall(fd, buff, 1)) > 0) {
    char *ch = buff;

    if (*ch == '\\' || *ch == '%') {
      if (readall(fd, buff, 1) <= 0) perror_exit("readall");
      if (*ch == 's') fputs(u.sysname, stdout);
      if (*ch == 'n'|| *ch == 'h') fputs(u.nodename, stdout);
      if (*ch == 'r') fputs(u.release, stdout);
      if (*ch == 'm') fputs(u.machine, stdout);
      if (*ch == 'l') fputs(TT.tty_name, stdout);
    } else xputc(*ch);
  }
}

// Read login name and print prompt and Issue file. 
static int read_login_name(void)
{
  tcflush(STDIN_FILENO, TCIFLUSH); // Flush pending speed switches
  int i = 0;

  while (1) { // Option -i will overide -f
    if (!(toys.optflags & FLAG_i)) write_issue(TT.issue_str); 
    print_prompt();
    TT.buff[0] = getchar();
    if (!TT.buff[0] && TT.sc > 1) return 0; // Switch speed
    if (TT.buff[0] == '\n') continue;
    if (TT.buff[0] != '\n')
      if (!fgets(&TT.buff[1], HOSTNAME_SIZE-1, stdin)) _exit(1);
    while (i < HOSTNAME_SIZE-1 && isgraph(TT.buff[i])) i++;
    TT.buff[i] = 0;
    break;
  }
  return 1;
}

// Put hostname entry in utmp file
static void utmp_entry(void)
{
  struct utmp entry;
  struct utmp *utp_ptr;
  pid_t pid = getpid();
  char *utmperr = "can't make utmp entry, host length greater than UT_HOSTSIZE(256)";

  utmpname(_PATH_UTMP);
  setutent(); // Starts from start
  while ((utp_ptr = getutent())) 
    if (utp_ptr->ut_pid == pid && utp_ptr->ut_type >= INIT_PROCESS) break;
  if (!utp_ptr) { 
    entry.ut_type = LOGIN_PROCESS;
    entry.ut_pid = getpid();
    xstrncpy(entry.ut_line, ttyname(STDIN_FILENO) + 
        strlen("/dev/"), UT_LINESIZE);
    time((time_t *)&entry.ut_time);
    xstrncpy(entry.ut_user, "LOGIN", UT_NAMESIZE);
    if (strlen(TT.host_str) > UT_HOSTSIZE) 
      perror_msg(utmperr);
    else xstrncpy(entry.ut_host, TT.host_str, UT_HOSTSIZE);
    setutent();
    pututline(&entry);
    return;
  }
  xstrncpy(entry.ut_line, ttyname(STDIN_FILENO) + strlen("/dev/"), UT_LINESIZE);
  xstrncpy(entry.ut_user, "LOGIN", UT_NAMESIZE);
  if (strlen(TT.host_str) > UT_HOSTSIZE) 
    perror_msg(utmperr);
  else xstrncpy(entry.ut_host, TT.host_str, UT_HOSTSIZE);
  time((time_t *)&entry.ut_time);
  setutent();
  pututline(&entry);
}

void getty_main(void)
{
  pid_t pid = getpid();
  char *ptr[3] = {"/bin/login", NULL, NULL}; //2 NULLs so we can add username

  if (!(toys.optflags & FLAG_f)) TT.issue_str = "/etc/issue";
  if (toys.optflags & FLAG_l) ptr[0] = TT.login_str;
  parse_arguments();
  open_tty();
  termios_init();
  tcsetpgrp(STDIN_FILENO, pid);
  if (toys.optflags & FLAG_H) utmp_entry();
  if (toys.optflags & FLAG_I) 
    writeall(STDOUT_FILENO,TT.init_str,strlen(TT.init_str));
  if (toys.optflags & FLAG_m) sense_baud();
  if (toys.optflags & FLAG_t) alarm(TT.timeout);
  if (toys.optflags & FLAG_w) {
    char ch;

    while (readall(STDIN_FILENO, &ch, 1) != 1)  
      if (ch == '\n' || ch == '\r') break;
  }
  if (!(toys.optflags & FLAG_n)) {
    int index = 1; // 0th we already set.

    while (1) {
      int l = read_login_name();

      if (l) break;
      index = index % TT.sc;
      cfsetspeed(&TT.termios, TT.speeds[index]); // Select from multiple speeds
      //Necessary after cfsetspeed
      if (tcsetattr(STDIN_FILENO, TCSANOW, &TT.termios) < 0) 
        perror_exit("tcsetattr"); 
    }
    ptr[1]=TT.buff; //put the username in the login command line
  }
  xexec(ptr);
}
