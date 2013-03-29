/* uudecode.c - uudecode / base64 decode
 *
 * Copyright 2013 Erich Plondke <toybox@erich.wreck.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/uudecode.html

USE_UUDECODE(NEWTOY(uudecode, ">1o:", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_UMASK))

config UUDECODE
  bool "uudecode"
  default n
  help
    usage: uudecode [-o OUTFILE] [INFILE]

    Decode file from stdin (or INFILE).

    -o	write to OUTFILE instead of filename in header
*/

#define FOR_uudecode
#include "toys.h"

GLOBALS(
  char *o;
)

static inline signed char uudecode_b64_1byte(char in)
{
  if (in >= 'A' && in <= 'Z') return in-'A';
  if (in >= 'a' && in <= 'z') return in-'a'+26;
  if (in >= '0' && in <= '9') return in-'0'+52;
  if (in == '+') return 62;
  if (in == '/') return 63;

  return -1;
};


static int uudecode_b64_4bytes(char *out, char *in)
{
  int i, len = 3;
  unsigned x = 0;

  for (i=0; i<4; i++) {
    int c = in[i];

    if (c == '=') len--;
    else if (len != 3) len = 0;
    if (!len) error_exit("bad input");

    x |= uudecode_b64_1byte(c) << (6*(3-i));
    if (i && i <= len) *(out++) = (x>>(8*(3-i))) & 0xff;
  }

  return len;
}

static void uudecode_b64_line(int ofd, char *in, int ilen)
{
  int olen;
  char out[4];

  while (ilen >= 4) {
    olen = uudecode_b64_4bytes(out, in);
    xwrite(ofd,out,olen);
    in += 4;
    ilen -= 4;
  };
}

static void uudecode_b64(int ifd, int ofd)
{
  int len;
  char *line;

  while ((line = get_line(ifd)) != NULL) {
    if ((len = strlen(line)) < 4) continue;
    if (!strncmp(line, "====", 4)) return;
    uudecode_b64_line(ofd,line,len);
    free(line);
  }
}


static void uudecode_uu_4bytes(char *out, char *in, int len)
{
  unsigned int i,x=0;

  for (i = 0; i < 4; i++) x |= ((in[i] - 32) & 0x03f) << (6*(3-i));
  if (len > 3) len = 3;
  for (i = 0; i < len; i++) *out++ = x >> (8*(2-i));
}

static void uudecode_uu_line(int ofd, char *in)
{
  int olen = in[0] - 32;
  char buf[4];

  in++;
  while (olen > 0) {
    uudecode_uu_4bytes(buf,in,olen);
    xwrite(ofd,buf,olen < 3 ? olen : 3);
    olen -= 3;
    in += 4;
  }
}

static void uudecode_uu(int ifd, int ofd)
{
  char *line = NULL;

  for (;;) {
    if (!(line = get_line(ifd))) break;
    if (*line) {
      if (!strncmp(line, "end", 3)) {
        free(line);
        break;
      }
      uudecode_uu_line(ofd,line);
    }
    free(line);
  }
}

void uudecode_main(void)
{
  int ifd = 0, ofd, idx = 0;
  char *line;
  void (*decoder)(int ifd, int ofd) = NULL;

  if (toys.optc) ifd = xopen(*toys.optargs, O_RDONLY);

  for (;;) {
    char mode[16];

    if (!(line = get_line(ifd))) error_exit("no header");
    sscanf(line, "begin%*[ ]%15s%*[ ]%n", mode, &idx);
    if (idx) decoder = uudecode_uu;
    else {
      sscanf(line, "begin-base64%*[ ]%15s%*[ ]%n", mode, &idx);
      if (idx) decoder = uudecode_b64;
    }

    if (!idx) {
      free(line);
      continue;
    }

    ofd = xcreate(TT.o ? TT.o : line+idx, O_WRONLY|O_CREAT|O_TRUNC,
      string_to_mode(mode, 0777^toys.old_umask));
    free(line);
    decoder(ifd,ofd);
    break;
  }

  if (CFG_TOYBOX_FREE) {
    if (ifd) close(ifd);
    close(ofd);
  }
}
