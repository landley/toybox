/* interestingtimes.c - cursor control
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 */

#include "toys.h"

int tty_fd(void)
{
  int i, j;

  for (i = 0; i<3; i++) if (isatty(j = (i+1)%3)) return j;

  return notstdio(open("/dev/tty", O_RDWR));
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
    if (isatty(i) && !ioctl(i, TIOCGWINSZ, &ws)) {
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

// Query terminal size, sending ANSI probe if necesary. (Probe queries xterm
// size through serial connection, when local TTY doesn't know but remote does.)
// Returns 0 if ANSI probe sent, 1 if size determined from tty or environment

int terminal_probesize(unsigned *xx, unsigned *yy)
{
  if (terminal_size(xx, yy) && (!xx || *xx) && (!yy || *yy)) return 1;

  // Send probe: bookmark cursor position, jump to bottom right,
  // query position, return cursor to bookmarked position.
  xprintf("\033[s\033[999C\033[999B\033[6n\033[u");

  return 0;
}

void xsetspeed(struct termios *tio, int speed)
{
  int i, speeds[] = {50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
                    4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
                    500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
                    2500000, 3000000, 3500000, 4000000};

  // Find speed in table, adjust to constant
  for (i = 0; i < ARRAY_LEN(speeds); i++) if (speeds[i] == speed) break;
  if (i == ARRAY_LEN(speeds)) error_exit("unknown speed: %d", speed);
  cfsetspeed(tio, i+1+4081*(i>15));
}


// Reset terminal to known state, saving copy of old state if old != NULL.
int set_terminal(int fd, int raw, int speed, struct termios *old)
{
  struct termios termio;
  int i = tcgetattr(fd, &termio);

  // Fetch local copy of old terminfo, and copy struct contents to *old if set
  if (i) return i;
  if (old) *old = termio;

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

  if (speed) {
    int i, speeds[] = {50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
                    4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
                    500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
                    2500000, 3000000, 3500000, 4000000};

    // Find speed in table, adjust to constant
    for (i = 0; i < ARRAY_LEN(speeds); i++) if (speeds[i] == speed) break;
    if (i == ARRAY_LEN(speeds)) error_exit("unknown speed: %d", speed);
    cfsetspeed(&termio, i+1+4081*(i>15));
  }

  return tcsetattr(fd, TCSAFLUSH, &termio);
}

void xset_terminal(int fd, int raw, int speed, struct termios *old)
{
  if (-1 != set_terminal(fd, raw, speed, old)) return;

  sprintf(libbuf, "/proc/self/fd/%d", fd);
  libbuf[readlink0(libbuf, libbuf, sizeof(libbuf))] = 0;
  perror_exit("tcsetattr %s", libbuf);
}

struct scan_key_list {
  int key;
  char *seq;
} static const scan_key_list[] = {
  {KEY_UP, "\033[A"}, {KEY_DOWN, "\033[B"},
  {KEY_RIGHT, "\033[C"}, {KEY_LEFT, "\033[D"},

  {KEY_UP|KEY_SHIFT, "\033[1;2A"}, {KEY_DOWN|KEY_SHIFT, "\033[1;2B"},
  {KEY_RIGHT|KEY_SHIFT, "\033[1;2C"}, {KEY_LEFT|KEY_SHIFT, "\033[1;2D"},

  {KEY_UP|KEY_ALT, "\033[1;3A"}, {KEY_DOWN|KEY_ALT, "\033[1;3B"},
  {KEY_RIGHT|KEY_ALT, "\033[1;3C"}, {KEY_LEFT|KEY_ALT, "\033[1;3D"},

  {KEY_UP|KEY_CTRL, "\033[1;5A"}, {KEY_DOWN|KEY_CTRL, "\033[1;5B"},
  {KEY_RIGHT|KEY_CTRL, "\033[1;5C"}, {KEY_LEFT|KEY_CTRL, "\033[1;5D"},

  // VT102/VT220 escapes.
  {KEY_HOME, "\033[1~"},
  {KEY_INSERT, "\033[2~"},
  {KEY_DELETE, "\033[3~"},
  {KEY_END, "\033[4~"},
  {KEY_PGUP, "\033[5~"},
  {KEY_PGDN, "\033[6~"},
  // "Normal" "PC" escapes (xterm).
  {KEY_HOME, "\033OH"},
  {KEY_END, "\033OF"},
  // "Application" "PC" escapes (gnome-terminal).
  {KEY_HOME, "\033[H"},
  {KEY_END, "\033[F"},

  {KEY_FN+1, "\033OP"}, {KEY_FN+2, "\033OQ"}, {KEY_FN+3, "\033OR"},
  {KEY_FN+4, "\033OS"}, {KEY_FN+5, "\033[15~"}, {KEY_FN+6, "\033[17~"},
  {KEY_FN+7, "\033[18~"}, {KEY_FN+8, "\033[19~"}, {KEY_FN+9, "\033[20~"},
};

// Scan stdin for a keypress, parsing known escape sequences, including
// responses to screen size queries.
// Blocks for timeout_ms milliseconds, 0=return immediately, -1=wait forever.
// Returns 0-255=literal, -1=EOF, -2=TIMEOUT, -3=RESIZE, 256+= a KEY_ constant.
// Scratch space is necessary because last char of !seq could start new seq.
// Zero out first byte of scratch before first call to scan_key.
int scan_key_getsize(char *scratch, int timeout_ms, unsigned *xx, unsigned *yy)
{
  struct pollfd pfd;
  int maybe, i, j;
  char *test;

  for (;;) {
    pfd.fd = 0;
    pfd.events = POLLIN;
    pfd.revents = 0;

    maybe = 0;
    if (*scratch) {
      int pos[6];
      unsigned x, y;

      // Check for return from terminal size probe
      memset(pos, 0, 6*sizeof(int));
      scratch[(1+*scratch)&15] = 0;
      sscanf(scratch+1, "\033%n[%n%3u%n;%n%3u%nR%n", pos, pos+1, &y,
             pos+2, pos+3, &x, pos+4, pos+5);
      if (pos[5]) {
        // Recognized X/Y position, consume and return
        *scratch = 0;
        if (xx) *xx = x;
        if (yy) *yy = y;
        return -3;
      } else for (i=0; i<6; i++) if (pos[i]==*scratch) maybe = 1;

      // Check sequences
      for (i = 0; i<ARRAY_LEN(scan_key_list); i++) {
        test = scan_key_list[i].seq;
        for (j = 0; j<*scratch; j++) if (scratch[j+1] != test[j]) break;
        if (j == *scratch) {
          maybe = 1;
          if (!test[j]) {
            // We recognized current sequence: consume and return
            *scratch = 0;
            return 256+scan_key_list[i].key;
          }
        }
      }

      // If current data can't be a known sequence, return next raw char
      if (!maybe) break;
    }

    // Need more data to decide

    // 30ms is about the gap between characters at 300 baud
    if (maybe || timeout_ms != -1)
      if (!xpoll(&pfd, 1, maybe ? 30 : timeout_ms)) break;

    // Read 1 byte so we don't overshoot sequence match. (We can deviate
    // and fail to match, but match consumes entire buffer.)
    if (toys.signal>0 || 1 != read(0, scratch+1+*scratch, 1))
      return (toys.signal>0) ? -3 : -1;
    ++*scratch;
  }

  // Was not a sequence
  if (!*scratch) return -2;
  i = scratch[1];
  if (--*scratch) memmove(scratch+1, scratch+2, *scratch);

  return i;
}

// Wrapper that ignores results from ANSI probe to update screensize.
// Otherwise acts like scan_key_getsize().
int scan_key(char *scratch, int timeout_ms)
{
  return scan_key_getsize(scratch, timeout_ms, NULL, NULL);
}

void tty_esc(char *s)
{
  printf("\033[%s", s);
}

void tty_jump(int x, int y)
{
  char s[32];

  sprintf(s, "%d;%dH", y+1, x+1);
  tty_esc(s);
}

void tty_reset(void)
{
  set_terminal(0, 0, 0, 0);
  tty_esc("?25h");
  tty_esc("0m");
  tty_jump(0, 999);
  tty_esc("K");
  fflush(0);
}

// If you call set_terminal(), use sigatexit(tty_sigreset);
void tty_sigreset(int i)
{
  tty_reset();
  _exit(i ? 128+i : 0);
}

void start_redraw(unsigned *width, unsigned *height)
{
  // If never signaled, do raw mode setup.
  if (!toys.signal) {
    *width = 80;
    *height = 25;
    set_terminal(0, 1, 0, 0);
    sigatexit(tty_sigreset);
    xsignal(SIGWINCH, generic_signal);
  }
  if (toys.signal != -1) {
    toys.signal = -1;
    terminal_probesize(width, height);
  }
  xprintf("\033[H\033[J");
}
