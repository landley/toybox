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
    (percent escapes for cdeEfgGiosuxX, slash escapes for \abefnrtv0 or
    \0OCT or 0xHEX).
*/

#define FOR_printf
#include "toys.h"

GLOBALS(
  char *hv_w;
  char *hv_p;
  int encountered;
)

// Detect matching character (return true/valse) and advance pointer if match.
static int eat(char **s, char c)
{
  int x = (**s == c);

  if (x) ++*s;

  return x;
}

// Add ll and L to Interger and floating point formats respectively.
static char *get_format(char *f)
{
  int len = strlen(f);
  char last = f[--len], *post = "";

  f[len] = 0;
  if (strchr("diouxX", last)) post = "ll";  // add ll to integer modifier.
  else if (strchr("feEgG", last)) post = "L"; // add L to float modifier.
  return xmprintf("%s%s%c", f, post, last);
}

// Print arguments with corresponding conversion and width and precision.
static void print(char *fmt, int w, int p)
{
  char *ptr = fmt, *ep = 0, *format = 0, *arg = *toys.optargs;

  errno = 0;
  if (strchr("diouxX", *ptr)) {
    long long val = 0;

    if (arg) {
      if (*arg == '\'' || *arg == '"') val = arg[1];
      else {
        val = strtoll(arg, &ep, 0);
        if (errno || (ep && (*ep || ep == arg))) {
          perror_msg("Invalid num %s", arg);
          val = 0;
        }
      }
    }
    format = get_format(fmt);
    TT.hv_w ? (TT.hv_p ? printf(format, w, p, val) : printf(format, w, val))
      : (TT.hv_p ? printf(format, p, val) : printf(format, val));
  } else if (strchr("gGeEf", *ptr)) {
    long double dval = 0;

    if (arg) {
      dval = strtold(arg, &ep);
      if (errno || (ep && (*ep || ep == arg))) {
        perror_msg("Invalid num %s", arg);
        dval = 0;
      }
    }
    format = get_format(fmt);
    TT.hv_w ? (TT.hv_p ? printf(format, w, p, dval) : printf(format, w, dval))
      : (TT.hv_p ? printf(format, p, dval) :  printf(format, dval));
  } else if (*ptr == 's') {
    char *str = arg;

    if (!str) str = "";

    TT.hv_w ? (TT.hv_p ? printf(fmt,w,p,str): printf(fmt, w, str))
      : (TT.hv_p ? printf(fmt, p, str) : printf(fmt, str));
  } else if (*ptr == 'c') printf(fmt, arg ? *arg : 0);

  if (format) free(format);
}

// Parse escape sequences.
static int handle_slash(char **esc_val)
{
  char *ptr = *esc_val;
  int len = 1, base = 0;
  unsigned result = 0;

  if (*ptr == 'c') xexit();

  // 0x12 hex escapes have 1-2 digits, \123 octal escapes have 1-3 digits.
  if (eat(&ptr, 'x')) base = 16;
  else if (*ptr >= '0' && *ptr <= '8') base = 8;
  len += (base-8)/8;

  // Not a hex or octal escape? (This catches trailing \)
  if (!len) {
    if (!(result = unescape(*ptr))) result = '\\';
    else ++*esc_val;

    return result;
  }

  while (len) {
    unsigned num = tolower(*ptr)-'0';

    if (num > 10) num += '0'-'a'+10;
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

  return (char)result;
}

void printf_main(void)
{
  char *format = *toys.optargs, **arg = toys.optargs+1, *f, *p;

  for (f = format; *f; f++) {
    if (eat(&f, '\\')) putchar(handle_slash(&f));
    else if (*f != '%' || *++f == '%') xputc(*f);
    else if (*f == 'b')
      for (p = *arg ? *(arg++) : ""; *p; p++) 
        putchar(eat(&p, '\\') ? handle_slash(&p) : *p);
    else {
      char *start = f;
      int wp[2], i;

      // todo: we currently ignore these?
      if (strchr("-+# ", *f)) f++;
      memset(wp, 0, 8);
      for (i=0; i<2; i++) {
        if (eat(&f, '*')) {
          if (*arg) wp[i] = atolx(*(arg++));
        } else while (isdigit(*f)) f++;
        if (!eat(&f, '.')) break;
      }
      if (!(p = strchr("diouxXfeEgGcs", *f)))
        perror_exit("bad format@%ld", f-format);
      else {
        int len = f-start;

        TT.hv_p = strstr(start, ".*");
        TT.hv_w = strchr(start, '*');
        //pitfall: handle diff b/w * and .*
        if ((TT.hv_w-1) == TT.hv_p) TT.hv_w = NULL;
        memcpy((p = xzalloc(len+1)), start, len);
        print(p+len-1, wp[0], wp[1]);
        if (*arg) arg++;
        free(p);
        p = NULL;
      } 
      TT.encountered = 1;
    }
  }
}
