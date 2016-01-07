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

// Wrapper that parses results from ANSI probe. If block|2 query size and
// send ANSI probe if we can't. (Probe queries xterm size through
// serial connection, when local TTY doesn't know and only remote end knows.)
// Otherwise acts like scan_key()
int scan_key_getsize(char *scratch, int block, unsigned *xx, unsigned *yy)
{
  int key;

  if (block&2) {
    struct pollfd pfd;

    if (terminal_size(xx, yy)) {
      if (!(block&1)) return -2;
    } else {
      // Send probe: bookmark cursor position, jump to bottom right,
      // query position, return cursor to bookmarked position.
      xprintf("\e[s\e[999C\e[999B\e[6n\e[u");

      // Wait up to a quarter second for a result
      memset(&pfd, 0, sizeof(struct pollfd));
      pfd.events = POLLIN;
      xpoll(&pfd, 0, 250);
    }
  }
  while (512&(key = scan_key(scratch, block&1))) {
    if (key<0) break;
    if (xx) *xx = (key>>10)&1023;
    if (yy) *yy = (key>>20)&1023;
  }

  return key;
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

struct scan_key_list {
  char *name, *seq;
} static const scan_key_list[] = TAGGED_ARRAY(KEY,
  // up down right left pgup pgdn home end ins
  {"UP", "\033[A"}, {"DOWN", "\033[B"}, {"RIGHT", "\033[C"}, {"LEFT", "\033[D"},
  {"PGUP", "\033[5~"}, {"PGDN", "\033[6~"}, {"HOME", "\033OH"},
  {"END", "\033OF"}, {"INSERT", "\033[2~"},
  {"SUP", "\033[1;2A"}, {"AUP", "\033[1;3A"}, {"CUP", "\033[1;5A"},
  {"SDOWN", "\033[1;2B"}, {"ADOWN", "\033[1;3B"}, {"CDOWN", "\033[1;5B"},
  {"SRIGHT", "\033[1;2C"}, {"ARIGHT", "\033[1;3C"}, {"CRIGHT", "\033[1;5C"},
  {"SLEFT", "\033[1;2D"}, {"ALEFT", "\033[1;3D"}, {"CLEFT", "\033[1;5D"}

);

// Scan stdin for a keypress, parsing known escape sequences
// Returns: 0-255=literal, -1=EOF, -2=NONE, 256-...=index into scan_key_list
// >512 is x<<9+y<<21
// scratch space is necessary because last char of !seq could start new seq
// Zero out first byte of scratch before first call to scan_key
// block=0 allows fetching multiple characters before updating display
int scan_key(char *scratch, int block)
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
      sscanf(scratch+1, "\033%n[%n%3u%n,%n%3u%nR%n", pos, pos+1, &y,
             pos+2, pos+3, &x, pos+4, pos+5);
      if (pos[5]) {
        // Recognized X/Y position, consume and return
        *scratch = 0;
        return 512+(x<<10)+(y<<20);
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

    // Read 1 byte so we don't overshoot sequence match. (We can deviate
    // and fail to match, but match consumes entire buffer.)
    if (1 != read(0, scratch+1+*scratch, 1)) return -1;
    ++*scratch;
  }

  // Was not a sequence
  if (!*scratch) return -2;
  i = scratch[1];
  if (--*scratch) memmove(scratch+1, scratch+2, *scratch);

  return i;
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
  set_terminal(1, 0, 0);
  tty_esc("?25h");
  tty_esc("0m");
  tty_jump(0, 999);
  tty_esc("K");
  fflush(0);
}

void tty_sigreset(int i)
{
  tty_reset();
  _exit(128+i);
}
