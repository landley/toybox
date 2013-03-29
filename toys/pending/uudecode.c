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

static int uudecode_b64_4bytes(char *out, char *in)
{
  int i, len = 3;
  unsigned x = 0;

  for (i=0; i<4; i++) {
    int c = in[i];

    if (c == '=') {
      len--;
      c = 'A';
    } else if (len != 3) len = 0;
    if (!len) goto bad;

    if (c >= 'A' && c <= 'Z') c -= 'A';
    else if (c >= 'a' && c <= 'z') c += 26 - 'a';
    else if (c >= '0' && c <= '9') c += 52 - '0';
    else if (c == '+') c = 62;
    else if (c == '/') c =63;
    else goto bad;

    x |= c << (6*(3-i));
    if (i && i <= len) *(out++) = (x>>(8*(3-i))) & 0xff;
  }

  return len;

bad:
  error_exit("bad input");
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

static void uudecode_uu_4bytes(char *out, char *in, int len)
{
  unsigned int i,x=0;

  for (i = 0; i < 4; i++) x |= ((in[i] - 32) & 0x03f) << (6*(3-i));
  if (len > 3) len = 3;
  for (i = 0; i < len; i++) *out++ = x >> (8*(2-i));
}

static void uudecode_uu_line(int ofd, char *in)
{
  int olen = (in[0] - 32) & 0x3f;
  char buf[4];

  in++;
  while (olen > 0) {
    uudecode_uu_4bytes(buf,in,olen);
    xwrite(ofd,buf,olen < 3 ? olen : 3);
    olen -= 3;
    in += 4;
  }
}

void uudecode_main(void)
{
  int ifd = 0, ofd, idx = 0, m;
  char *line = 0,
       *class[] = {"begin%*[ ]%15s%*[ ]%n", "begin-base64%*[ ]%15s%*[ ]%n"};


  if (toys.optc) ifd = xopen(*toys.optargs, O_RDONLY);

  for (;;) {
    char mode[16];

    free(line);
    if (!(line = get_line(ifd))) error_exit("bad EOF");
    for (m=0; m < 2; m++)  {
      sscanf(line, class[m], mode, &idx);
      if (idx) break;
    }

    if (!idx) continue;

    ofd = xcreate(TT.o ? TT.o : line+idx, O_WRONLY|O_CREAT|O_TRUNC,
      string_to_mode(mode, 0777^toys.old_umask));
    free(line);
    break;
  }

  while ((line = get_line(ifd)) != NULL) {
    if (strcmp(line, m ? "====" : "end")) {
      if (m) uudecode_b64_line(ofd, line, strlen(line));
      else uudecode_uu_line(ofd, line);
    } else m = 2;
    free(line);
    if (m == 2) break;
  }

  if (CFG_TOYBOX_FREE) {
    if (ifd) close(ifd);
    close(ofd);
  }
}
