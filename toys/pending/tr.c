/* tr.c - translate or delete characters
 *
 * Copyright 2014 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/tr.html

USE_TR(NEWTOY(tr, "^>2<1Ccsd[+cC]", TOYFLAG_USR|TOYFLAG_BIN))

config TR
  bool "tr"
  default n
  help
    usage: tr [-cds] SET1 [SET2]

    Translate, squeeze, or delete characters from stdin, writing to stdout

    -c/-C  Take complement of SET1
    -d     Delete input characters coded SET1
    -s     Squeeze multiple output characters of SET2 into one character
*/

#define FOR_tr
#include "toys.h"

GLOBALS(
  short map[256]; //map of chars
  int len1, len2;
)

enum {
  class_alpha, class_alnum, class_digit,
  class_lower,class_upper,class_space,class_blank,
  class_punct,class_cntrl,class_xdigit,class_invalid
};

static void map_translation(char *set1 , char *set2)
{
  int i = TT.len1, k = 0;

  if (toys.optflags & FLAG_d)
    for (; i; i--, k++) TT.map[set1[k]] = set1[k]|0x100; //set delete bit

  if (toys.optflags & FLAG_s) {
    for (i = TT.len1, k = 0; i; i--, k++)
      TT.map[set1[k]] = TT.map[set1[k]]|0x200;
    for (i = TT.len2, k = 0; i; i--, k++)
      TT.map[set2[k]] = TT.map[set2[k]]|0x200;
  }
  i = k = 0;
  while (!(toys.optflags & FLAG_d) && set2 && TT.len1--) { //ignore set2 if -d present
    TT.map[set1[i]] = ((TT.map[set1[i]] & 0xFF00) | set2[k]);
    if (set2[k + 1]) k++;
    i++;
  }
}

static int handle_escape_char(char **esc_val) //taken from printf
{
  char *ptr = *esc_val;
  int esc_length = 0;
  unsigned  base = 0, num = 0, result = 0, count = 0;

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

static int find_class(char *class_name)
{
  int i;
  static char *class[] = {
    "[:alpha:]","[:alnum:]","[:digit:]",
    "[:lower:]","[:upper:]","[:space:]",
    "[:blank:]","[:punct:]","[:cntrl:]",
    "[:xdigit:]","NULL"
  };

  for (i = 0; i != class_invalid; i++) {
    if (!memcmp(class_name, class[i], (class_name[0] == 'x')?10:9)) break;
  }
  return i;
}

static char *expand_set(char *arg, int *len)
{
  int i = 0, j, k, size = 256;
  char *set = xzalloc(size*sizeof(char));

  while (*arg) {

    if (i >= size) {
      size += 256;
      set = xrealloc(set, size);
    }
    if (*arg == '\\') {
      arg++;
      set[i++] = (int)handle_escape_char(&arg);
      arg++;
      continue;
    }
    if (arg[1] == '-') {
      if (arg[2] == '\0') goto save;
      j = arg[0];
      k = arg[2];
      if (j > k) perror_exit("reverse colating order");
      while (j <= k) set[i++] = j++;
      arg += 3;
      continue;
    }
    if (arg[0] == '[' && arg[1] == ':') {

      if ((j = find_class(arg)) == class_invalid) goto save;

      if ((j == class_alpha) || (j == class_upper) || (j == class_alnum)) {
      for (k = 'A'; k <= 'Z'; k++) set[i++] = k;
      }
      if ((j == class_alpha) || (j == class_lower) || (j == class_alnum)) {
        for (k = 'a'; k <= 'z'; k++) set[i++] = k;
      }
      if ((j == class_alnum) || (j == class_digit) || (j == class_xdigit)) {
        for (k = '0'; k <= '9'; k++) set[i++] = k;
      }
      if (j == class_space || j == class_blank) {
        set[i++] = '\t';
        if (j == class_space) {
          set[i++] = '\n';
          set[i++] = '\f';
          set[i++] = '\r';
          set[i++] = '\v';
        }
        set[i++] = ' ';
      }
      if (j == class_punct) {
        for (k = 0; k <= 255; k++)
          if (ispunct(k)) set[i++] = k;
      }
      if (j == class_cntrl) {
        for (k = 0; k <= 255; k++)
          if (iscntrl(k)) set[i++] = k;
      }
      if (j == class_xdigit) {
        for (k = 'A'; k <= 'F'; k++) {
          set[i + 6] = k | 0x20;
          set[i++] = k;
        }
        i += 6;
        arg += 10;
        continue;
      }

      arg += 9; //never here for class_xdigit.
      continue;
    }
    if (arg[0] == '[' && arg[1] == '=') { //[=char=] only
      arg += 2;
      if (*arg) set[i++] = *arg;
      if (!arg[1] || arg[1] != '=' || arg[2] != ']')
        error_exit("bad equiv class");
      continue;
    }
save:
    set[i++] = *arg++;
  }
  *len = i;
  return set;
}

static void print_map(char *set1, char *set2)
{
  int r = 0, i, prev_char = -1;

  while (1)
  {
    i = 0;
    r = read(STDIN_FILENO, (toybuf), sizeof(toybuf));
    if (!r) break;
    for (;r > i;i++) {

      if ((toys.optflags & FLAG_d) && (TT.map[(int)toybuf[i]] & 0x100)) continue;
      if (toys.optflags & FLAG_s) {
        if ((TT.map[(int)toybuf[i]] & 0x200) &&
            (prev_char == TT.map[(int)toybuf[i]])) {
          continue;
        }
      }
      xputc(TT.map[(int)toybuf[i]] & 0xFF);
      prev_char = TT.map[(int)toybuf[i]];
      fflush(stdout);
    }
  }
}

static void do_complement(char **set)
{
  int i, j;
  char *comp = xmalloc(256);

  for (i = 0, j = 0;i < 256; i++) {
    if (memchr(*set, i, TT.len1)) continue;
    else comp[j++] = (char)i;
  }
  free(*set);
  TT.len1 = j;
  *set = comp;
}

void tr_main(void)
{
  char *set1, *set2 = NULL;
  int i;

  for (i = 0; i < 256; i++) TT.map[i] = i; //init map

  set1 = expand_set(toys.optargs[0], &TT.len1);
  if (toys.optflags & FLAG_c) do_complement(&set1);
  if (toys.optargs[1]) {
    if (toys.optargs[1][0] == '\0') error_exit("set2 can't be empty string");
    set2 = expand_set(toys.optargs[1], &TT.len2);
  }
  map_translation(set1, set2);

  print_map(set1, set2);
  free(set1);
  free(set2);
}
