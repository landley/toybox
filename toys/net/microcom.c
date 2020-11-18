/* microcom.c - Simple serial console.
 *
 * Copyright 2017 The Android Open Source Project.

USE_MICROCOM(NEWTOY(microcom, "<1>1s#=115200X", TOYFLAG_USR|TOYFLAG_BIN))

config MICROCOM
  bool "microcom"
  default y
  help
    usage: microcom [-s SPEED] [-X] DEVICE

    Simple serial console.

    -s	Set baud rate to SPEED (default 115200)
    -X	Ignore ^@ (send break) and ^] (exit)
*/

#define FOR_microcom
#include "toys.h"

GLOBALS(
  long s;

  int fd, stok;
  struct termios old_stdin, old_fd;
)

// TODO: tty_sigreset outputs ansi escape sequences, how to disable?
static void restore_states(int i)
{
  if (TT.stok) tcsetattr(0, TCSAFLUSH, &TT.old_stdin);
  tcsetattr(TT.fd, TCSAFLUSH, &TT.old_fd);
}

void microcom_main(void)
{
  struct termios tio;
  struct pollfd fds[2];
  int i;

  // Open with O_NDELAY, but switch back to blocking for reads.
  TT.fd = xopen(*toys.optargs, O_RDWR | O_NOCTTY | O_NDELAY);
  if (-1==(i = fcntl(TT.fd, F_GETFL, 0)) || fcntl(TT.fd, F_SETFL, i&~O_NDELAY)
      || tcgetattr(TT.fd, &TT.old_fd))
    perror_exit_raw(*toys.optargs);

  // Set both input and output to raw mode.
  memcpy(&tio, &TT.old_fd, sizeof(struct termios));
  cfmakeraw(&tio);
  xsetspeed(&tio, TT.s);
  if (tcsetattr(TT.fd, TCSAFLUSH, &tio)) perror_exit("set speed");
  if (!set_terminal(0, 1, 0, &TT.old_stdin)) TT.stok++;
  // ...and arrange to restore things, however we may exit.
  sigatexit(restore_states);

  fds[0].fd = TT.fd;
  fds[1].fd = 0;
  fds[0].events = fds[1].events = POLLIN;

  while (poll(fds, 2, -1) > 0) {

    // Read from connection, write to stdout.
    if (fds[0].revents) {
      if (0 < (i = read(TT.fd, toybuf, sizeof(toybuf)))) xwrite(0, toybuf, i);
      else break;
    }

    // Read from stdin, write to connection.
    if (fds[1].revents) {
      if (read(0, toybuf, 1) != 1) break;
      if (!FLAG(X)) {
        if (!*toybuf) {
          tcsendbreak(TT.fd, 0);
          continue;
        } else if (*toybuf == (']'-'@')) break;
      }
      xwrite(TT.fd, toybuf, 1);
    }
  }
}
