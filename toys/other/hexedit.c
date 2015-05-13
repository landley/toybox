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

    Hexadecimal file editor.

    -r	Read only (display but don't edit)
*/

#define FOR_hexedit
#include "toys.h"

GLOBALS(
  char *data;
  long long len, base;
  int numlen;
  unsigned height;
)

static void esc(char *s)
{
  printf("\033[%s", s);
}

static void jump(int x, int y)
{
  char s[32];

  sprintf(s, "%d;%dH", y+1, x+1);
  esc(s);
}

static void fix_terminal(void)
{
  set_terminal(1, 0, 0);
  esc("?25h");
  esc("0m");
  jump(0, 999);
  esc("K");
}

static void sigttyreset(int i)
{
  fix_terminal();
  // how do I re-raise the signal so it dies with right signal info for wait()?
  _exit(127);
}

// Render all characters printable, using color to distinguish.
static void draw_char(char broiled)
{
  if (broiled<32 || broiled>=127) {
    if (broiled>127) {
      esc("2m");
      broiled &= 127;
    }
    if (broiled<32 || broiled==127) {
      esc("7m");
      if (broiled==127) broiled = 32;
      else broiled += 64;
    }
    printf("%c", broiled);
    esc("0m");
  } else printf("%c", broiled);
}

static void draw_tail(void)
{
  int i = 0, width = 0, w, len;
  char *start = *toys.optargs, *end;

  jump(0, TT.height);
  esc("K");

  // First time, make sure we fit in 71 chars (advancing start as necessary).
  // Second time, print from start to end, escaping nonprintable chars.
  for (i=0; i<2; i++) {
    for (end = start; *end;) {
      wchar_t wc;

      len = mbrtowc(&wc, end, 99, 0);
      if (len<0 || wc<32 || (w = wcwidth(wc))<0) {
        len = w = 1;
        if (i) draw_char(*end);
      } else if (i) fwrite(end, len, 1, stdout);
      end += len;

      if (!i) {
        width += w;
        while (width > 71) {
          len = mbrtowc(&wc, start, 99, 0);
          if (len<0 || wc<32 || (w = wcwidth(wc))<0) len = w = 1;
          width -= w;
          start += len;
        }
      }
    }
  }
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
    for (x=0; x<xx; x++) draw_char(TT.data[yy+x]);
    printf("%*s", 16-xx, "");
  }
  esc("K");
}

static void draw_page(void)
{
  int y;

  jump(0, 0);
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
  jump(2+TT.numlen+3*xx, yy);
  esc("0m");
  if (side!=2) esc("7m");
  if (side>1) printf("%02X", cc);
  else for (i=0; i<2;) {
    if (side==i) esc("32m");
    printf("%X", (cc>>(4*(1&++i)))&15);
  }
  esc("0m");
  jump(TT.numlen+17*3+xx, yy);
  draw_char(cc);
}

#define KEY_UP 256
#define KEY_DOWN 257
#define KEY_RIGHT 258
#define KEY_LEFT 259
#define KEY_PGUP 260
#define KEY_PGDN 261
#define KEY_HOME 262
#define KEY_END  263
#define KEY_INSERT 264

void hexedit_main(void)
{
  // up down right left pgup pgdn home end ins
  char *keys[] = {"\033[A", "\033[B", "\033[C", "\033[D", "\033[5~", "\033[6~",
                  "\033OH", "\033OF", "\033[2~", 0};
  long long pos;
  int x, y, i, side = 0, key, ro = toys.optflags&FLAG_r,
      fd = xopen(*toys.optargs, ro ? O_RDONLY : O_RDWR);

  TT.height = 25;
  terminal_size(0, &TT.height);
  if (TT.height) TT.height--;
  sigatexit(sigttyreset);
  esc("0m");
  esc("?25l");
  fflush(0);
  set_terminal(1, 1, 0);

  if ((TT.len = fdlength(fd))<0) error_exit("bad length");
  if (sizeof(long)==32 && TT.len>SIZE_MAX) TT.len = SIZE_MAX;
  // count file length hex digits, rounded up to multiple of 4
  for (pos = TT.len, TT.numlen = 0; pos; pos >>= 4, TT.numlen++);
  TT.numlen += (4-TT.numlen)&3;

  TT.data = mmap(0, TT.len, PROT_READ|(PROT_WRITE*!ro), MAP_SHARED, fd, 0);

  draw_page();

  y = x = 0;
  for (;;) {
    // Get position within file, trimming if we overshot end.
    pos = 16*(TT.base+y)+x;
    if (pos>=TT.len) {
      pos = TT.len-1;
      x = pos&15;
      y = (pos/16)-TT.base;
    }

    // Display cursor
    highlight(x, y, ro ? 3 : side);
    xprintf("");

    // Wait for next key
    key = scan_key(toybuf, keys, 1);
    // Exit for q, ctrl-c, ctrl-d, escape, or EOF
    if (key==-1 || key==3 || key==4 || key==27 || key=='q') break;
    highlight(x, y, 2);

    if (key>='a' && key<='f') key-=32;
    if (!ro && ((key>='0' && key<='9') || (key>='A' && key<='F'))) {
      i = key - '0';
      if (i>9) i -= 7;
      TT.data[pos] &= 15<<(4*side);
      TT.data[pos] |= i<<(4*!side);

      highlight(x, y, ++side);
      if (side==2) {
        side = 0;
        if (++pos<TT.len && ++x==16) {
          x = 0;
          if (++y == TT.height) {
            --y;
            goto down;
          }
        }
      }
    }
    if (key>255) side = 0;
    if (key==KEY_UP) {
      if (--y<0) {
        if (TT.base) {
          TT.base--;
          esc("1T");
          draw_tail();
          jump(0, 0);
          draw_line(0);
        }
        y = 0;
      }
    } else if (key==KEY_DOWN) {
      if (y == TT.height-1 && (pos|15)+1<TT.len) {
down:
        TT.base++;
        esc("1S");
        jump(0, TT.height-1);
        draw_line(TT.height-1);
        draw_tail();
      }
      if (++y>=TT.height) y--;
    } else if (key==KEY_RIGHT) {
      if (x<15 && pos+1<TT.len) x++;
    } else if (key==KEY_LEFT) {
      if (x) x--;
    } else if (key==KEY_PGUP) {
      TT.base -= TT.height;
      if (TT.base<0) TT.base = 0;
      draw_page();
    } else if (key==KEY_PGDN) {
      TT.base += TT.height;
      if ((TT.base*16)>=TT.len) TT.base=(TT.len-1)/16;
      while ((TT.base+y)*16>=TT.len) y--;
      if (16*(TT.base+y)+x>=TT.len) x = (TT.len-1)&15;
      draw_page();
    } else if (key==KEY_HOME) {
      TT.base = 0;
      x = 0;
      draw_page();
    } else if (key==KEY_END) {
      TT.base=(TT.len-1)/16;
      x = (TT.len-1)&15;
      draw_page();
    }
  }
  munmap(TT.data, TT.len);
  close(fd);
  fix_terminal();
}
