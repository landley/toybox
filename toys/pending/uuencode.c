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

    Uuencode or (with -m option) base64-encode stdin or [file], 
    with encode-filename in the output, which is sent to stdout.
*/

#define FOR_uuencode
#include "toys.h"



static void uuencode_b64_3bytes(char *out, const char *in, int bytes)
{
  static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789+/";
  unsigned int i,x=0;
  for (i = 0; i < bytes; i++) {
    x |= (in[i] & 0x0ff) << (8*(2-i));
  }
  out[0] = table[(x>>(3*6)) & 0x3f];
  out[1] = table[(x>>(2*6)) & 0x3f];
  out[2] = table[(x>>(1*6)) & 0x3f];
  out[3] = table[(x>>(0*6)) & 0x3f];
  if (bytes <= 1) out[2] = '=';
  if (bytes <= 2) out[3] = '=';
}

static void uuencode_b64_line(char *out, const char *in, int len)
{
  while (len > 0) {
    uuencode_b64_3bytes(out,in,len < 3 ? len : 3);
    xprintf("%c%c%c%c",out[0],out[1],out[2],out[3]);
    in += 3;
    len -= 3;
  };
  xprintf("\n");
}

static void uuencode_b64(int fd, const char *name)
{
  int len;
  char *inbuf = toybuf;
  char *outbuf = toybuf+64;
  xprintf("begin-base64 744 %s\n",name);
  do {
    len = xread(fd,inbuf,48);
    uuencode_b64_line(outbuf,inbuf,len);
  } while (len > 0);
  xprintf("====\n");
}


static void uuencode_uu_3bytes(char *out, const char *in)
{
  unsigned int i,x=0;
  for (i = 0; i <= 2; i++) {
    x |= (in[i] & 0x0ff) << (8*(2-i));
  }
  for (i = 0; i <= 3; i++) {
    out[i] = 32 + ((x >> (6*(3-i))) & 0x3f);
  }
}

static void uuencode_uu_line(char *out, const char *in, int len)
{
  int i;
  if (len == 0) {
    xprintf("`\n");
    return;
  }
  xprintf("%c",len+32);
  for (i = 0; i < len; i += 3) {
    uuencode_uu_3bytes(out,in+i);
    xprintf("%c%c%c%c",out[0],out[1],out[2],out[3]);
  }
  xprintf("\n");
}

static void uuencode_uu(int fd, const char *name)
{
  int len;
  char *inbuf = toybuf;
  char *outbuf = toybuf+64;
  xprintf("begin 744 %s\n",name);
  do {
    len = xread(fd,inbuf,45);
    uuencode_uu_line(outbuf,inbuf,len);
  } while (len > 0);
  xprintf("end\n");
}

void uuencode_main(void)
{
  char *encode_filename = toys.optargs[0];
  int fd = 0; /* STDIN */

  if (toys.optc == 2) {
    fd = xopen(toys.optargs[0],O_RDONLY); // dies if error
    encode_filename = toys.optargs[1];
  }
  if (toys.optflags & FLAG_m) uuencode_b64(fd,encode_filename);
  else uuencode_uu(fd,encode_filename);
}
