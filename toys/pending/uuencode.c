/* uuencode.c - uuencode / base64 encode
 *
 * Copyright 2013 Erich Plondke <toybox@erich.wreck.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/uuencode.html

USE_UUENCODE(NEWTOY(uuencode, "<1>2m", TOYFLAG_USR|TOYFLAG_BIN))

config UUENCODE
  bool "uuencode"
  default n
  help
    usage: uuencode [-m] [file] encode-filename

    Uuencode stdin (or file) to stdout, with encode-filename in the output.

    -m	base64-encode
*/

#define FOR_uuencode
#include "toys.h"

// Convert 3 bytes of input to 4 bytes of output
static void uuencode_b64_3bytes(const char *in, int bytes)
{
  unsigned int i, x;

  for (i = x = 0; i<4; i++) {
    if (i < bytes) x |= (in[i] & 0x0ff) << (8*(2-i));
    xputc(i > bytes ? '=' : toybuf[(x>>((3-i)*6)) & 0x3f]);
  } 
}

static void uuencode_b64_line(const char *in, int len)
{
  while (len > 0) {
    uuencode_b64_3bytes(in, len < 3 ? len : 3);
    in += 3;
    len -= 3;
  };
  xprintf("\n");
}

static void uuencode_b64(int fd, const char *name)
{
  int len;
  char buf[(76/4)*3];

  xprintf("begin-base64 744 %s\n",name);
  do {
    len = xread(fd, buf, sizeof(buf));
    
    uuencode_b64_line(buf, len);
  } while (len > 0);
  xprintf("====\n");
}


static void uuencode_uu_3bytes(const char *in)
{
  unsigned int i, x = 0;

  for (i = 0; i <= 2; i++) x |= (in[i] & 0x0ff) << (8*(2-i));
  for (i = 0; i <= 3; i++) xputc(32 + ((x >> (6*(3-i))) & 0x3f));
}

static void uuencode_uu_line(char *in, int len)
{
  if (len > 0) {
    int i;

    xputc(len+32);
    for (i = 0; i < len; i += 3) uuencode_uu_3bytes(in+i);
  }
  xprintf("\n");
}

static void uuencode_uu(int fd, const char *name)
{
  int len;
  char buf[45];

  xprintf("begin 744 %s\n",name);
  do {
    len = xread(fd, buf, 45);
    uuencode_uu_line(buf, len);
  } while (len > 0);
  xprintf("end\n");
}

void uuencode_main(void)
{
  char *p, *name = toys.optargs[0];
  int i, fd = 0;

  // base64 table

  p = toybuf;
  for (i = 'A'; i != ':'; i++) {
    if (i == 'Z'+1) i = 'a';
    if (i == 'z'+1) i = '0';
    *(p++) = i;
  }
  *(p++) = '+';
  *(p++) = '/';

  if (toys.optc == 2) {
    fd = xopen(toys.optargs[0], O_RDONLY); // dies if error
    name = toys.optargs[1];
  }
  if (toys.optflags & FLAG_m) uuencode_b64(fd, name);
  else uuencode_uu(fd, name);
}
