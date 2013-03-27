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

/*
 * Turn a character back into a value.
 * The smallest valid character is 0x2B ('+')
 * The biggest valid character is 0x7A ('z')
 * We can make a table of 16*5 entries to cover 0x2B - 0x7A
 */

static inline signed char uudecode_b64_1byte(char in)
{
  char ret;
  static const signed char table[16*5] = {
    /* '+' (0x2B) is 62, '/'(0x2F) is 63, rest invalid */
                                                62, -1, -1, -1, 63,
    /* '0'-'9' are values 52-61, rest of 0x3A - 0x3F is invalid, = is special... */
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    /* '@' is invalid, 'A'-'Z' are values 0-25, 0x5b - 0x5F are invalid */
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    /* '`' is invalid, 'a'-'z' are values 26-51, 0x7B - 0x7F are invalid */
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
  };

  in &= 0x7f;
  if (in < '+') return -1;
  if (in > 'z') return -1;
  in -= '+';
  ret = table[in];

  return ret;
};


/* Returns length put in out */
static int uudecode_b64_4bytes(char *out, const char *in)
{
  unsigned int i, x=0;
  signed char b0, b1, b2, b3;
  int len = 3;

  b0 = uudecode_b64_1byte(in[0]);
  b1 = uudecode_b64_1byte(in[1]);
  b2 = uudecode_b64_1byte(in[2]);
  b3 = uudecode_b64_1byte(in[3]);
  if ((b1 < 0) || (b0 < 0)) return 0;
  if (b3 < 0) len--;
  if (b2 < 0) len--;
  x = ((b0 & 0x3f)<<18) | ((b1 & 0x3f)<<12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
  for (i = 0; i < len; i++) *out++ = (x>>(8*(2-i))) & 0x0ff;

  return len;
}

static void uudecode_b64_line(int ofd, const char *in, int ilen)
{
  int olen;
  char out[4];

  while (ilen >= 4) {
    olen = uudecode_b64_4bytes(out,in);
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


static void uudecode_uu_4bytes(char *out, const char *in, int len)
{
  unsigned int i,x=0;

  for (i = 0; i < 4; i++) x |= ((in[i] - 32) & 0x03f) << (6*(3-i));
  if (len > 3) len = 3;
  for (i = 0; i < len; i++) *out++ = x >> (8*(2-i));
}

static void uudecode_uu_line(int ofd, const char *in)
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

  while ((line = get_line(ifd)) != NULL) {
    if (line[0] == '`') break;
    if (!strncmp(line, "end", 3)) break;
    if (strlen(line) < 1) break;
    uudecode_uu_line(ofd,line);
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

    if (!idx) continue;

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
