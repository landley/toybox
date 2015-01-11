/* printf.c - Format and Print the data.
 *
 * Copyright 2014 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html

USE_PRINTF(NEWTOY(printf, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config PRINTF 
  bool "printf"
  default n
  help
    usage: printf FORMAT [ARGUMENT...]
    
    Format and print ARGUMENT(s) according to FORMAT, using C printf syntax
    (% escapes for cdeEfgGiosuxX, \ escapes for abefnrtv0 or \OCTAL or \xHEX).
*/

#define FOR_printf
#include "toys.h"

// Detect matching character (return true/valse) and advance pointer if match.
static int eat(char **s, char c)
{
  int x = (**s == c);

  if (x) ++*s;

  return x;
}

// Parse escape sequences.
static int handle_slash(char **esc_val)
{
  char *ptr = *esc_val;
  int len, base = 0;
  unsigned result = 0, num;

  if (*ptr == 'c') xexit();

  // 0x12 hex escapes have 1-2 digits, \123 octal escapes have 1-3 digits.
  if (eat(&ptr, 'x')) base = 16;
  else if (*ptr >= '0' && *ptr <= '8') base = 8;
  len = (char []){0,3,2}[base/8];

  // Not a hex or octal escape? (This catches trailing \)
  if (!len) {
    if (!(result = unescape(*ptr))) result = '\\';
    else ++*esc_val;

    return result;
  }

  while (len) {
    num = tolower(*ptr) - '0';
    if (num >= 'a'-'0') num += '0'-'a'+10;
    if (num >= base) {
      // Don't parse invalid hex value ala "\xvd", print it verbatim
      if (base == 16 && len == 2) {
        ptr--;
        result = '\\';
      }
      break;
    }
    result = (result*base)+num;
    ptr++;
    len--;
  }
  *esc_val = ptr;

  return result;
}

void printf_main(void)
{
  char **arg = toys.optargs+1;

  // Repeat format until arguments consumed
  for (;;) {
    int seen = 0;
    char *f = *toys.optargs;

    // Loop through characters in format
    while (*f) {
      if (eat(&f, '\\')) putchar(handle_slash(&f));
      else if (!eat(&f, '%') || *f == '%') putchar(*f++);

      // Handle %escape
      else {
        char c, *start = f, *end = 0, *aa, *width = "";
        int wp[] = {-1,-1}, i;
        union {
          int c; // type promotion
          char *str;
          long long ll;
          double dd;
        } mash;

        // Parse width.precision between % and type indicator.
        // todo: we currently ignore these?
        if (strchr("-+# ", *f)) f++;
        for (i=0; i<2; i++) {
          if (eat(&f, '*')) {
            if (*arg) wp[i] = atolx(*arg++);
          } else while (isdigit(*f)) f++;
          if (!eat(&f, '.')) break;
        }
        seen++;
        errno = 0;
        c = *f++;
        aa = *arg ? *arg++ : "";

        // Handle %esc, assembling new format string into toybuf if necessary.
        if ((f-start) > sizeof(toybuf)-4) c = 0;
        if (c == 'b') {
          while (*aa) putchar(eat(&aa, '\\') ? handle_slash(&aa) : *aa++);

          continue;
        } else if (c == 'c') mash.c = *aa;
        else if (c == 's') mash.str = aa;
        else if (strchr("diouxX", c)) {
          width = "ll";
          if (*aa == '\'' || *aa == '"') mash.ll = aa[1];
          else mash.ll = strtoll(aa, &end, 0);
        } else if (strchr("feEgG", c)) mash.dd = strtod(aa, &end);
        else error_exit("bad %%%c@%ld", c, f-*toys.optargs);

        if (end && (errno || *end)) perror_msg("bad %%%c %s", c, aa);

        sprintf(toybuf, "%%%.*s%s%c", (int)(f-start)-1, start, width, c);
        wp[0]>=0
          ? (wp[1]>=0 ? printf(toybuf, wp[0], wp[1], mash)
                      : printf(toybuf, wp[0], mash))
          : (wp[1]>=0 ? printf(toybuf, wp[1], mash)
                      : printf(toybuf, mash));
      }
    }

    // Posix says to keep looping through format until we consume all args.
    // This only works if the format actually consumed at least one arg.
    if (!seen || !*arg) break;
  }
}
