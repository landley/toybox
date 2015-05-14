/* interestingtimes.c - cursor control
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 */

#include "toys.h"

int xgettty(void)
{
  int i, j;

  for (i = 0; i<3; i++) if (isatty(j = (i+1)%3)) return j;

  return xopen("/dev/tty", O_RDWR);
}

// Quick and dirty query size of terminal, doesn't do ANSI probe fallback.
// set x=80 y=25 before calling to provide defaults. Returns 0 if couldn't
// determine size.

int terminal_size(unsigned *xx, unsigned *yy)
{
  struct winsize ws;
  unsigned i, x = 0, y = 0;
  char *s;

  // stdin, stdout, stderr
  for (i=0; i<3; i++) {
    memset(&ws, 0, sizeof(ws));
    if (!ioctl(i, TIOCGWINSZ, &ws)) {
      if (ws.ws_col) x = ws.ws_col;
      if (ws.ws_row) y = ws.ws_row;

      break;
    }
  }
  s = getenv("COLUMNS");
  if (s) sscanf(s, "%u", &x);
  s = getenv("LINES");
  if (s) sscanf(s, "%u", &y);

  // Never return 0 for either value, leave it at default instead.
  if (xx && x) *xx = x;
  if (yy && y) *yy = y;

  return x || y;
}

// Reset terminal to known state, saving copy of old state if old != NULL.
int set_terminal(int fd, int raw, struct termios *old)
{
  struct termios termio;

  // Fetch local copy of old terminfo, and copy struct contents to *old if set
  if (!tcgetattr(fd, &termio) && old) *old = termio;

  // the following are the bits set for an xterm. Linux text mode TTYs by
  // default add two additional bits that only matter for serial processing
  // (turn serial line break into an interrupt, and XON/XOFF flow control)

  // Any key unblocks output, swap CR and NL on input
  termio.c_iflag = IXANY|ICRNL|INLCR;
  if (toys.which->flags & TOYFLAG_LOCALE) termio.c_iflag |= IUTF8;

  // Output appends CR to NL, does magic undocumented postprocessing
  termio.c_oflag = ONLCR|OPOST;

  // Leave serial port speed alone
  // termio.c_cflag = C_READ|CS8|EXTB;

  // Generate signals, input entire line at once, echo output
  // erase, line kill, escape control characters with ^
  // erase line char at a time
  // "extended" behavior: ctrl-V quotes next char, ctrl-R reprints unread chars,
  // ctrl-W erases word
  termio.c_lflag = ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|IEXTEN;

  if (raw) cfmakeraw(&termio);

  return tcsetattr(fd, TCSANOW, &termio);
}

// Scan stdin for a keypress, parsing known escape sequences
// seqs is array of char * strings, ends with NULL ptr
// Returns: 0-255=literal, -1=EOF, -2=NONE, 256-...=index into seq
// scratch space is necessary because last char of !seq could start new seq
// Zero out first byte of scratch before first call to scan_key
// block=0 allows fetching multiple characters before updating display
int scan_key(char *scratch, char **seqs, int block)
{
  struct pollfd pfd;
  int maybe, i, j;
  char *test;

  for (;;) {
    pfd.fd = 0;
    pfd.events = POLLIN;
    pfd.revents = 0;

    // check sequences
    maybe = 0;
    if (*scratch) {
      for (i = maybe = 0; (test = seqs[i]); i++) {
        for (j = 0; j<*scratch; j++) if (scratch[j+1] != test[j]) break;
        if (j == *scratch) {
          maybe = 1;
          if (!test[j]) {
            // We recognized current sequence: consume and return
            *scratch = 0;
            return 256+i;
          }
        }
      }
      // If current data can't be a known sequence, return next raw char
      if (!maybe) break;
    }

    // Need more data to decide

    // 30 miliseconds is about the gap between characters at 300 baud 
    if (maybe || !block) if (!xpoll(&pfd, 1, 30*maybe)) break;

    if (1 != read(0, scratch+1+*scratch, 1)) return -1;
    ++*scratch;
  }

  // Was not a sequence
  if (!*scratch) return -2;
  i = scratch[1];
  if (--*scratch) memmove(scratch+1, scratch+2, *scratch);

  return i;
}
