/* hexedit.c - Hexadecimal file editor
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * No standard

USE_HEXEDIT(NEWTOY(hexedit, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config HEXEDIT
  bool "hexedit"
  default n
  help
    usage: hexedit FILENAME

    Hexadecimal file editor.
*/

#define FOR_hexedit
#include "toys.h"

GLOBALS(
  char *data;
  long long len, base;
  int numlen;
  unsigned height;
)

static void sigttyreset(int i)
{
  set_terminal(1, 0, 0);
  // how do I re-raise the signal so it dies with right signal info for wait()?
  _exit(127);
}

static void esc(char *s)
{
  printf("\033[%s", s);
}

static void jump(x, y)
{
  char s[32];

  sprintf(s, "%d;%dH", y+1, x+1);
  esc(s);
}

static void draw_line(long long yy)
{
  int x;

  yy = (TT.base+yy)*16;

  if (yy<TT.len) {
    printf("\r%0*llX ", TT.numlen, yy);
    for (x=0; x<16; x++) {
      if (yy+x<TT.len) printf(" %02X", TT.data[yy+x]);
      else printf("   ");
    }
    printf("  ");
    for (x=0; x<16; x++) printf("X");
  }
  esc("K");
}

static void highlight(char c, unsigned x, unsigned y)
{
  jump(2+TT.numlen+3*x, y);
  printf("%02X", c);
}

void draw_page(void)
{
  int y;

  jump(0, 0);
  for (y = 0; y<TT.height; y++) {
    if (y) printf("\r\n");
    draw_line(y);
  }
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
  int x, y, key, fd = xopen(*toys.optargs, O_RDONLY);

  TT.height = 25;
  terminal_size(0, &TT.height);
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

  TT.data = mmap(0, TT.len, PROT_READ, MAP_SHARED, fd, 0);

  draw_page();

  y = x = 0;
  for (;;) {
    pos = 16*(TT.base+y)+x;
    if (pos>=TT.len) {
      pos = TT.len-1;
      x = (TT.len-1)%15;
    }
    esc("7m");
    highlight(TT.data[pos], x, y);
    fflush(0);
    key = scan_key(toybuf, keys, 1);
    if (key==-1 || key==4 || key==27) break;
    esc("0m");
    highlight(TT.data[pos], x, y);

//jump(73,0);
//printf("%d[%c]", key, (key > 255) ? 'X' : key);

    if (key==KEY_UP) {
      if (--y<0) {
        if (TT.base) {
          TT.base--;
          esc("1T");
          jump(0, 0);
          draw_line(0);
        }
        y = 0;
      }
    } else if (key==KEY_DOWN) {
      if (y == TT.height-1 && pos+32<TT.len) {
        TT.base++;
        esc("1S");
        jump(0, TT.height-1);
        draw_line(TT.height-1);
      }
      if (pos+16<TT.len && ++y>=TT.height) y--;
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
      draw_page();
    } else if (key==KEY_HOME) {
      TT.base = 0;
      draw_page();
    } else if (key==KEY_END) {
      TT.base=(TT.len-1)/16;
      draw_page();
    }
  }
  munmap(TT.data, TT.len);
  close(fd);
  set_terminal(1, 0, 0);
  esc("?25h");
  esc("0m");
  jump(0, 999);
  xputc('\n');

// DEBUG: dump unknown escape sequence
for (;;) {
  key = scan_key(toybuf, keys, 0);
  if (key >= 0) printf("%d(%c) ", key, key);
  else {
    printf("%d\n", key);
    break;
  }
}
xputc('\n');
}
