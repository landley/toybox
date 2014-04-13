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
    usage: printf Format [Arguments..]
    
    Format and print ARGUMENT(s) according to FORMAT.
    Format is 'C' control output as 'C' printf.
*/
#define FOR_printf
#include "toys.h"

GLOBALS(
  char *hv_w;
  char *hv_p;
  int encountered;
)

// Calculate width and precision from format string
static int get_w_p()
{
  char *ptr, *str = *toys.optargs;
  
  errno = 0;
  if (*str == '-') str++;
  long value = strtol(str, &ptr, 10);
  if (errno || (ptr && (*ptr != '\0' || ptr == str)))
    perror_msg("Invalid num %s", *toys.optargs);
  if (*--str == '-') return (int)(-1 * value);
  return value;
}

// Add ll and L to Interger and floating point formats respectively.
static char *get_format(char *f)
{
  int len = strlen(f);
  char last = f[--len], *post = "";

  f[len] = '\0';
  if (strchr("diouxX", last)) post = "ll";  // add ll to integer modifier.
  else if (strchr("feEgG", last)) post = "L"; // add L to float modifier.
  return xmprintf("%s%s%c", f, post, last);
}

// Print arguments with corresponding conversion and width and precision.
static void print(char *fmt, int w, int p, int l)
{
  char *ptr = (fmt+l-1), *ep = NULL, *format = NULL;
  long long val;
  long double dval;
  
  errno = 0;
  switch (*ptr) {
    case 'd': /*FALL_THROUGH*/
    case 'i': /*FALL_THROUGH*/
    case 'o': /*FALL_THROUGH*/
    case 'u': /*FALL_THROUGH*/
    case 'x': /*FALL_THROUGH*/
    case 'X':
      if (!*toys.optargs) val = 0;
      else {
        if (**toys.optargs == '\'' || **toys.optargs == '"')
          val = *((*toys.optargs) + 1);
        else {
          val = strtoll(*toys.optargs, &ep, 0);
          if (errno || (ep && (*ep != '\0' || ep == *toys.optargs))) {
            perror_msg("Invalid num %s", *toys.optargs);
            val = 0;
          }
        }
      }
      format = get_format(fmt);
      TT.hv_w ? (TT.hv_p ? printf(format, w, p, val) : printf(format, w, val))
        : (TT.hv_p ? printf(format, p, val) : printf(format, val));
      break;
    case 'g': /*FALL_THROUGH*/
    case 'e': /*FALL_THROUGH*/
    case 'E': /*FALL_THROUGH*/
    case 'G': /*FALL_THROUGH*/
    case 'f':
      if (*toys.optargs) {
        dval = strtold(*toys.optargs, &ep);
        if (errno || (ep && (*ep != '\0' || ep == *toys.optargs))) {
          perror_msg("Invalid num %s", *toys.optargs);
          dval = 0;
        }
      } else dval = 0;
      format = get_format(fmt);
      TT.hv_w ? (TT.hv_p ? printf(format, w, p, dval):printf(format, w, dval))
        : (TT.hv_p ? printf(format, p, dval) :  printf(format, dval));
      break;
    case 's':
      {
        char *str = (*toys.optargs ? *toys.optargs : "");
        TT.hv_w ? (TT.hv_p ? printf(fmt,w,p,str): printf(fmt, w, str))
          : (TT.hv_p ? printf(fmt, p, str) : printf(fmt, str));
      }
      break;
    case 'c':
      printf(fmt, (*toys.optargs ? **toys.optargs : '\0'));
      break;
  }
  if (format) free(format);
}

// Handle the escape sequences.
static int handle_slash(char **esc_val)
{
  char *ptr = *esc_val;
  int esc_length = 0;
  unsigned  base = 0, num = 0, result = 0, count = 0;
  
  /*
   * Hex escape sequence have only 1 or 2 digits, xHH. Oct escape sequence 
   * have 1,2 or 3 digits, xHHH. Leading "0" (\0HHH) we are ignoring.
   */
  if (*ptr == 'x') {
    ptr++;
    esc_length++;
    base = 16;
  } else if (isdigit(*ptr)) base = 8;

  while (esc_length < 3 && base) {
    num = tolower(*ptr) - '0';
    if (num > 10) num += ('0' - 'a' + 10);
    if (num >= base) {
      if (base == 16) {
        esc_length--;
        if (!esc_length) {// Invalid hex value eg. /xvd, print as it is /xvd
          result = '\\';
          ptr--;
        }
      }
      break;
    }
    esc_length++;
    count = result = (count * base) + num;
    ptr++;
  }
  if (base) {
    ptr--;
    *esc_val = ptr;
    return (char)result;
  } else {
    switch (*ptr) {
      case 'n':  result = '\n'; break;
      case 't':  result = '\t'; break;
      case 'e':  result = (char)27; break;
      case 'b':  result = '\b'; break;
      case 'a':  result = '\a'; break;
      case 'f':  result = '\f'; break;
      case 'v':  result = '\v'; break;
      case 'r':  result = '\r'; break;
      case '\\': result = '\\'; break;
      default :
        result = '\\';
        ptr--; // Let pointer pointing to / we will increment after returning.
        break;
    }
  }
  *esc_val = ptr;
  return (char)result;
}

// Handle "%b" option with '\' interpreted.
static void print_esc_str(char *str)              
{
  for (; *str; str++) {
    if (*str == '\\') {
      str++;
      xputc(handle_slash(&str)); //print corresponding char
    } else xputc(*str);
  }
}

// Prase the format string and print.
static void parse_print(char *f)
{
  char *start, *p, format_specifiers[] = "diouxXfeEgGcs";
  int len = 0, width = 0, prec = 0;

  while (*f) {
    switch (*f) {
      case '%':
        start = f++;
        len++;
        if (*f == '%') {
          xputc('%');
          break;
        }
        if (*f == 'b') {
          if (*toys.optargs) {
            print_esc_str(*toys.optargs++);
            TT.encountered = 1;
          } else print_esc_str("");
          break;
        }
        if (strchr("-+# ", *f)) f++, len++;
        if (*f == '*') {
          f++, len++;
          if (*toys.optargs) {
            width = get_w_p();
            toys.optargs++;
          }
        } else {
          while (isdigit(*f)) f++, len++;
        }
        if (*f == '.') {
          f++, len++;
          if (*f == '*') {
            f++, len++;
            if (*toys.optargs) {
              prec = get_w_p();
              toys.optargs++;
            }
          } else {
            while (isdigit(*f)) f++, len++;
          }
        }
        if (!(p = strchr(format_specifiers, *f)))
          perror_exit("Missing OR Invalid format specifier");
        else {
          len++;
          TT.hv_p = strstr(start, ".*");
          TT.hv_w = strchr(start, '*');
          //pitfall: handle diff b/w * and .*
          if ((TT.hv_w-1) == TT.hv_p) TT.hv_w = NULL;
          memcpy((p = xzalloc(len+1)), start, len);
          print(p, width, prec, len);
          if (*toys.optargs) toys.optargs++;
          free(p);
          p = NULL;
        } 
        TT.encountered = 1;
        break;
      case '\\':
        if (f[1]) {
          if (*++f == 'c') exit(0); //Got '\c', so no further output  
          xputc(handle_slash(&f));
        } else xputc(*f);
        break;
      default:
        xputc(*f);
        break;
    }
    f++;
    len = 0;
  }
}

void printf_main(void)
{
  char *format = *toys.optargs++;

  TT.encountered = 0;
  parse_print(format); //printf acc. to format.
  //Re-use FORMAT arg as necessary to convert all given ARGS.
  while (*toys.optargs && TT.encountered) parse_print(format);
  xflush();
}
