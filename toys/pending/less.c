/* less.c - opposite of more
 *
 * Copyright 2016 Toni Spets <toni.spets@iki.fi>
 *
 * No standard.

USE_LESS(NEWTOY(less, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config LESS
  bool "less"
  default n
  help
    usage: less FILE
*/

#define FOR_less
#include "toys.h"

GLOBALS(
  struct linestack *ls;
  struct screen *scr;

  // view offset
  int x_off;
  int y_off;
  int y_max;
)

static void redraw(void)
{
  unsigned i;

  for (i = 0; i < TT.scr->h - 1; i++) {
    char *row = SCR_PTR(TT.scr) + (TT.scr->l * i);
    int len = (int)TT.ls->idx[i + TT.y_off].len - TT.x_off;
    if (len > TT.scr->l) len = TT.scr->l;

    if (i + TT.y_off < TT.ls->len) {
      if (TT.x_off < TT.ls->idx[i + TT.y_off].len) {
        memcpy(row, (char *)TT.ls->idx[i + TT.y_off].ptr + TT.x_off, len);
        row[len] = 0;
      } else {
        *row = 0;
      }
    } else {
      memcpy(row, "~\0", 2);
    }
  }

  strncpy(SCR_PTR(TT.scr) + (TT.scr->l * (TT.scr->h - 1)), toybuf, TT.scr->l);

  scr_update(TT.scr);
}

void less_main(void)
{
  char scratch[16];
  unsigned run = 1;

  if (!isatty(0) || !isatty(1))
    error_exit("no tty");

  if (!(TT.ls = linestack_load(*toys.optargs)))
    TT.ls = xzalloc(sizeof(struct linestack));

  TT.scr = scr_init();

  TT.y_max = TT.ls->len - TT.scr->h + 1;
  if (TT.y_max < 0) TT.y_max = 0;

  sprintf(toybuf, "%s%s", *toys.optargs, (TT.y_off == TT.y_max ? " (END)" : ""));

  *scratch = 0;
  do {
    int key;

    printf("\033[%u;%luH", TT.scr->h, strlen(toybuf) + 1);
    redraw();

    key = scan_key(scratch, -1);

    sprintf(toybuf, ":");

    switch (key) {
      case 'q':   run = 0; break;
      case 0x100: // arrow up
      case 'k':   if (TT.y_off > 0) TT.y_off--; break;
      case 0x101: // arrow down
      case 0xD:   // return
      case 'j':   if (TT.y_off + TT.scr->h <= TT.ls->len) TT.y_off++; break;
      case 0x102: // arrow right
      case 'l':   TT.x_off += TT.scr->w; break;
      case 0x103: // arrow left
      case 'h':   if (TT.x_off > 0) TT.x_off -= TT.scr->w; break;
      case 0x15:  // ^U
        if (TT.y_off - (int)(TT.scr->h / 2) >= 0) TT.y_off -= TT.scr->h / 2;
        else TT.y_off = 0;
        break;
      case 0x4:   // ^D
        if (TT.y_off + (TT.scr->h / 2) <= TT.y_max) TT.y_off += TT.scr->h / 2;
        else TT.y_off = TT.y_max;
        break;
      case 0x104: // pg up
        if (TT.y_off - (int)TT.scr->h >= 0) TT.y_off -= TT.scr->h;
        else TT.y_off = 0;
        break;
      case 0x6:   // ^F
      case 0x20:  // space
      case 0x105: // pg dn
        if (TT.y_off + TT.scr->h <= TT.y_max) TT.y_off += TT.scr->h;
        else TT.y_off = TT.y_max;
        break;
      default:
        if (CFG_TOYBOX_DEBUG)
          sprintf(toybuf, "Unhandled key %d (%Xh)", key, key);
    }

    if (TT.y_off == TT.y_max)
      sprintf(toybuf, "(END)");

  } while (run);

  tty_reset();
}
