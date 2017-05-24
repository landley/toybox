/* hexedit.c - Hexadecimal file editor
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * No standard

USE_HEXEDIT(NEWTOY(hexedit, "<1>1r", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config HEXEDIT
  bool "hexedit"
  default y
  help
    usage: hexedit FILENAME

    Hexadecimal file editor. All changes are written to disk immediately.

    -r	Read only (display but don't edit)

    Keys:
    Arrows        Move left/right/up/down by one line/column
    Pg Up/Pg Dn   Move up/down by one page
    0-9, a-f      Change current half-byte to hexadecimal value
    u             Undo
    q/^c/^d/<esc> Quit
*/

#define FOR_hexedit
#include "toys.h"

GLOBALS(
  char *data;
  long long len, base;
  int numlen, undo, undolen;
  unsigned height;
)

#define UNDO_LEN (sizeof(toybuf)/(sizeof(long long)+1))

// Render all characters printable, using color to distinguish.
static int draw_char(FILE *fp, wchar_t broiled)
{
  if (fp) {
    if (broiled<32 || broiled>=127) {
      if (broiled>127) {
        tty_esc("2m");
        broiled &= 127;
      }
      if (broiled<32 || broiled==127) {
        tty_esc("7m");
        if (broiled==127) broiled = 32;
        else broiled += 64;
      }
      printf("%c", (int)broiled);
      tty_esc("0m");
    } else printf("%c", (int)broiled);
  }

  return 1;
}

static void draw_tail(void)
{
  tty_jump(0, TT.height);
  tty_esc("K");

  draw_trim(*toys.optargs, -1, 71);
}

static void draw_line(long long yy)
{
  int x, xx = 16;

  yy = (TT.base+yy)*16;
  if (yy+xx>=TT.len) xx = TT.len-yy;

  if (yy<TT.len) {
    printf("\r%0*llX ", TT.numlen, yy);
    for (x=0; x<xx; x++) printf(" %02X", TT.data[yy+x]);
    printf("%*s", 2+3*(16-xx), "");
    for (x=0; x<xx; x++) draw_char(stdout, TT.data[yy+x]);
    printf("%*s", 16-xx, "");
  }
  tty_esc("K");
}

static void draw_page(void)
{
  int y;

  tty_jump(0, 0);
  for (y = 0; y<TT.height; y++) {
    if (y) printf("\r\n");
    draw_line(y);
  }
  draw_tail();
}

// side: 0 = editing left, 1 = editing right, 2 = clear, 3 = read only
static void highlight(int xx, int yy, int side)
{
  char cc = TT.data[16*(TT.base+yy)+xx];
  int i;

  // Display cursor
  tty_jump(2+TT.numlen+3*xx, yy);
  tty_esc("0m");
  if (side!=2) tty_esc("7m");
  if (side>1) printf("%02X", cc);
  else for (i=0; i<2;) {
    if (side==i) tty_esc("32m");
    printf("%X", (cc>>(4*(1&++i)))&15);
  }
  tty_esc("0m");
  tty_jump(TT.numlen+17*3+xx, yy);
  draw_char(stdout, cc);
}

void hexedit_main(void)
{
  long long pos = 0, y;
  int x, i, side = 0, key, ro = toys.optflags&FLAG_r,
      fd = xopen(*toys.optargs, ro ? O_RDONLY : O_RDWR);
  char keybuf[16];

  *keybuf = 0;

  // Terminal setup
  TT.height = 25;
  terminal_size(0, &TT.height);
  if (TT.height) TT.height--;
  sigatexit(tty_sigreset);
  tty_esc("0m");
  tty_esc("?25l");
  fflush(0);
  xset_terminal(1, 1, 0);

  if ((TT.len = fdlength(fd))<1) error_exit("bad length");
  if (sizeof(long)==32 && TT.len>SIZE_MAX) TT.len = SIZE_MAX;
  // count file length hex in digits, rounded up to multiple of 4
  for (pos = TT.len, TT.numlen = 0; pos; pos >>= 4, TT.numlen++);
  TT.numlen += (4-TT.numlen)&3;

  TT.data = xmmap(0, TT.len, PROT_READ|(PROT_WRITE*!ro), MAP_SHARED, fd, 0);
  draw_page();

  for (;;) {
    // Scroll display if necessary
    if (pos<0) pos = 0;
    if (pos>=TT.len) pos = TT.len-1;
    x = pos&15;
    y = pos/16;

    i = 0;
    while (y<TT.base) {
      if (TT.base-y>(TT.height/2)) {
        TT.base = y;
        draw_page();
      } else {
        TT.base--;
        i++;
        tty_esc("1T");
        tty_jump(0, 0);
        draw_line(0);
      }
    }
    while (y>=TT.base+TT.height) {
      if (y-(TT.base+TT.height)>(TT.height/2)) {
        TT.base = y-TT.height-1;
        draw_page();
      } else {
        TT.base++;
        i++;
        tty_esc("1S");
        tty_jump(0, TT.height-1);
        draw_line(TT.height-1);
      }
    }
    if (i) draw_tail();
    y -= TT.base;

    // Display cursor and flush output
    highlight(x, y, ro ? 3 : side);
    xflush();

    // Wait for next key
    key = scan_key(keybuf, -1);
    // Exit for q, ctrl-c, ctrl-d, escape, or EOF
    if (key==-1 || key==3 || key==4 || key==27 || key=='q') break;
    highlight(x, y, 2);

    // Hex digit?
    if (key>='a' && key<='f') key-=32;
    if (!ro && ((key>='0' && key<='9') || (key>='A' && key<='F'))) {
      if (!side) {
        long long *ll = (long long *)toybuf;

        ll[TT.undo] = pos;
        toybuf[(sizeof(long long)*UNDO_LEN)+TT.undo++] = TT.data[pos];
        if (TT.undolen < UNDO_LEN) TT.undolen++;
        TT.undo %= UNDO_LEN;
      }

      i = key - '0';
      if (i>9) i -= 7;
      TT.data[pos] &= 15<<(4*side);
      TT.data[pos] |= i<<(4*!side);

      if (++side==2) {
        highlight(x, y, side);
        side = 0;
        ++pos;
      }
    } else side = 0;
    if (key=='u') {
      if (TT.undolen) {
        long long *ll = (long long *)toybuf;

        TT.undolen--;
        if (!TT.undo) TT.undo = UNDO_LEN;
        pos = ll[--TT.undo];
        TT.data[pos] = toybuf[sizeof(long long)*UNDO_LEN+TT.undo];
      }
    }
    if (key>=256) {
      key -= 256;

      if (key==KEY_UP) pos -= 16;
      else if (key==KEY_DOWN) pos += 16;
      else if (key==KEY_RIGHT) {
        if (x<15) pos++;
      } else if (key==KEY_LEFT) {
        if (x) pos--;
      } else if (key==KEY_PGUP) pos -= 16*TT.height;
      else if (key==KEY_PGDN) pos += 16*TT.height;
      else if (key==KEY_HOME) pos = 0;
      else if (key==KEY_END) pos = TT.len-1;
    }
  }
  munmap(TT.data, TT.len);
  close(fd);
  tty_reset();
}
