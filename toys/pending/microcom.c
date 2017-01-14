/* microcom.c - Simple serial console.
 *
 * Copyright 2017 The Android Open Source Project.

USE_MICROCOM(NEWTOY(microcom, "<1>1s:X", TOYFLAG_BIN))

config MICROCOM
  bool "microcom"
  default n
  help
    usage: microcom [-s SPEED] [-X]

    Simple serial console.

    -s  Set baud rate to SPEED
    -X  Ignore ^@ (send break) and ^X (exit).
*/

#define FOR_microcom
#include "toys.h"

GLOBALS(
  char *speed;

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
  if (speed && cfsetspeed(&t, speed)) perror_exit("cfsetspeed");

  if (tcsetattr(fd, TCSAFLUSH, &t)) perror_exit("tcsetattr %s", name);
}

static void restore_states(int i)
{
  tcsetattr(0, TCSAFLUSH, &TT.original_stdin_state);
  tcsetattr(TT.fd, TCSAFLUSH, &TT.original_fd_state);
}

void microcom_main(void)
{
  speed_t speed = B115200;
  struct pollfd fds[2];
  int flags;

  // TODO: move this into lib and support all known baud rates?
  if (TT.speed) {
    switch (atolx(TT.speed)) {
    case 9600: speed = B9600; break;
    case 19200: speed = B19200; break;
    case 38400: speed = B38400; break;
    case 115200: speed = B115200; break;
    default: error_exit("unknown speed: %s", TT.speed);
    }
  }

  // Open with O_NDELAY, but switch back to blocking for reads.
  TT.fd = xopen(*toys.optargs, O_RDWR | O_NOCTTY | O_NDELAY);
  flags = fcntl(TT.fd, F_GETFL, 0);
  if (flags == -1) perror_exit("fcntl F_GETFL");
  if (fcntl(TT.fd, F_SETFL, flags & ~O_NDELAY)) perror_exit("fcntl F_SETFL");

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
      if (read(0, buf, 1) == 1) {
        if (buf[0] == 0) tcsendbreak(TT.fd, 0);
        else if (buf[0] == ('X'-'@')) break;
        else xwrite(TT.fd, buf, 1);
      } else break;
    }
  }
}
