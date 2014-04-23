/* more.c - View FILE (or stdin) one screenful at a time.
 *
 * Copyright 2013 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No Standard

USE_MORE(NEWTOY(more, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config MORE
  bool "more"
  default n
  help
    usage: more [FILE]...

    View FILE (or stdin) one screenful at a time.
*/

#define FOR_more
#include "toys.h"
#include <signal.h>

GLOBALS(
  struct termios inf;
  int cin_fd;
)

static void signal_handler(int sig)
{
  tcsetattr(TT.cin_fd, TCSANOW, &TT.inf);
  xputc('\n');
  signal(sig, SIG_DFL);
  raise(sig);
  _exit(sig | 128);
}

static void do_cat_operation(int fd, char *name)
{
  char *buf = NULL;
  
  if(toys.optc > 1) printf(":::::::::::::::::::::::\n"
      "%s\n:::::::::::::::::::::::\n",name);
  for (; (buf = get_line(fd)); free(buf)) printf("%s\n", buf);
}

void more_main()
{
  int ch, lines, input_key = 0, disp_more, more_msg_len;
  unsigned rows = 24, cols = 80;
  struct stat st;  
  struct termios newf;
  FILE *fp, *cin;

  if (!isatty(STDOUT_FILENO) || !(cin = fopen("/dev/tty", "r"))) {
    loopfiles(toys.optargs, do_cat_operation);
    toys.exitval = 0;
    return;
  }

  TT.cin_fd = fileno(cin);
  tcgetattr(TT.cin_fd,&TT.inf);
  //Prepare terminal for input
  memcpy(&newf, &TT.inf, sizeof(struct termios));
  newf.c_lflag &= ~(ICANON | ECHO);
  newf.c_cc[VMIN] = 1;
  newf.c_cc[VTIME] = 0;
  tcsetattr(TT.cin_fd, TCSANOW, &newf);

  sigatexit(signal_handler);

  do {
    fp = stdin;
    if (*toys.optargs && !(fp = fopen(*toys.optargs, "r"))) {
        perror_msg("'%s'", *toys.optargs);
        continue;
    }
    st.st_size = disp_more = more_msg_len = lines = 0;
    fstat(fileno(fp), &st);
    terminal_size(&cols, &rows);
    rows--;
    if(toys.optc > 1) {
      printf(":::::::::::::::::::::::\n"
          "%s\n:::::::::::::::::::::::\n",*toys.optargs);
      rows -= 3;
    }

    while ((ch = getc(fp)) != EOF) {
      if (input_key != 'r' && disp_more) {
        more_msg_len = printf("--More-- ");
        if (st.st_size) 
          more_msg_len += printf("(%d%% of %lld bytes)",
              (int) (100 * ( (double) ftell(fp) / (double) st.st_size)), 
              st.st_size);
        fflush(NULL);

        while (1) {
          input_key = getc(cin);
          input_key = tolower(input_key);
          printf("\r%*s\r", more_msg_len, ""); // Remove previous msg
          if (input_key == ' ' || input_key == '\n' || input_key == 'q' 
              || input_key == 'r') break;
          more_msg_len = printf("(Enter:Next line Space:Next page Q:Quit R:Show the rest)");
        }
        more_msg_len = lines = disp_more = 0;
        if (input_key == 'q') goto stop; 
        terminal_size(&cols, &rows);
        rows--;
      }

      if (ch == '\n') 
        if (++lines >= rows || input_key == '\n') disp_more = 1;
      putchar(ch);
    }
    fclose(fp);
    fflush(NULL);
  } while (*toys.optargs && *++toys.optargs);

stop:
  tcsetattr(TT.cin_fd, TCSANOW, &TT.inf);
  fclose(cin);
  toys.exitval = 0;
}
