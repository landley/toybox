/* microcom.c - Simple serial console.
 *
 * Copyright 2017 The Android Open Source Project.

USE_MICROCOM(NEWTOY(microcom, "<1>1s:X", TOYFLAG_BIN))

config MICROCOM
  bool "microcom"
  default y
  help
    usage: microcom [-s SPEED] [-X] DEVICE

    Simple serial console.

    -s  Set baud rate to SPEED
    -X  Ignore ^@ (send break) and ^] (exit).
*/

#define FOR_microcom
#include "toys.h"

GLOBALS(
  char *s;

  int fd;
  struct termios original_stdin_state, original_fd_state;
)

// Puts `fd` into raw mode, setting the baud rate if `speed` != 0,
// and saving the original terminal state.
static void xraw(int fd, const char *name, speed_t speed,
                 struct termios *original)
{
  struct termios t;

  if (tcgetattr(fd, &t)) perror_exit("tcgetattr %s", name);
  *original = t;

  cfmakeraw(&t);
  if (speed) cfsetspeed(&t, speed);

  if (tcsetattr(fd, TCSAFLUSH, &t)) perror_exit("tcsetattr %s", name);
}

static void restore_states(int i)
{
  tcsetattr(0, TCSAFLUSH, &TT.original_stdin_state);
  tcsetattr(TT.fd, TCSAFLUSH, &TT.original_fd_state);
}

void microcom_main(void)
{
  int speeds[] = {50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
                  4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
                  500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
                  2500000, 3000000, 3500000, 4000000};
  struct pollfd fds[2];
  int i, speed;

  if (!TT.s) speed = 115200;
  else speed = atoi(TT.s);

  // Find speed in table, adjust to constant
  for (i = 0; i < ARRAY_LEN(speeds); i++) if (speeds[i] == speed) break;
  if (i == ARRAY_LEN(speeds)) error_exit("unknown speed: %s", TT.s);
  speed = i+1+4081*(i>15);

  // Open with O_NDELAY, but switch back to blocking for reads.
  TT.fd = xopen(*toys.optargs, O_RDWR | O_NOCTTY | O_NDELAY);
  if (-1==(i = fcntl(TT.fd, F_GETFL, 0)) || fcntl(TT.fd, F_SETFL, i&~O_NDELAY))
    perror_exit_raw(*toys.optargs);

  // Set both input and output to raw mode.
  xraw(TT.fd, "fd", speed, &TT.original_fd_state);
  xraw(0, "stdin", 0, &TT.original_stdin_state);
  // ...and arrange to restore things, however we may exit.
  sigatexit(restore_states);

  fds[0].fd = TT.fd;
  fds[0].events = POLLIN;
  fds[1].fd = 0;
  fds[1].events = POLLIN;

  while (poll(fds, 2, -1) > 0) {
    char buf[BUFSIZ];

    // Read from connection, write to stdout.
    if (fds[0].revents) {
      ssize_t n = read(TT.fd, buf, sizeof(buf));
      if (n > 0) xwrite(0, buf, n);
      else break;
    }

    // Read from stdin, write to connection.
    if (fds[1].revents) {
      if (read(0, buf, 1) != 1) break;
      if (!(toys.optflags & FLAG_X)) {
        if (!*buf) {
          tcsendbreak(TT.fd, 0);
          continue;
        } else if (*buf == (']'-'@')) break;
      }
      xwrite(TT.fd, buf, 1);
    }
  }
}
