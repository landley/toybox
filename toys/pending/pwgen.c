/* pwgen.c - A password generator.
 *
 * Copyright 2020 Moritz Röhrich <moritz@ildefons.de>

USE_PWGEN(NEWTOY(pwgen, ">2?r(remove):c(capitalize)n(numerals)y(symbols)s(secure)B(ambiguous)h(help)C1vA(no-capitalize)0(no-numerals)[-cA][-n0]", TOYFLAG_USR|TOYFLAG_BIN))

config PWGEN
  bool "pwgen"
  default n
  help
    usage: pwgen [ OPTIONS ] [ length ] [ number ]

    A password generator.

    Options supported by pwgen:
      -c  --capitalize                  Permit capital letters.
      -A  --no-capitalize               Don't include capital letters.
      -n  --numerals                    Permit numbers.
      -0  --no-numerals                 Don't include numbers.
      -y  --symbols                     Permit special characters ($#%...).
      -r <chars>  --remove=<chars>      Don't include the given characters.
      -s  --secure                      Generate more random passwords.
      -B  --ambiguous                   Avoid ambiguous characters (e.g. 0, O).
      -h  --help                        Print this help message.
      -C                                Print the output in columns.
      -1                                Print the output one line each.
      -v                                Don't include vowels.
*/

#define FOR_pwgen
#include "toys.h"

GLOBALS(
  char *r;
)

// Generate one random printable/typeable ascii character.
char get_rand_chr() {
  return (char) (33 + (rand() % 93));
}

// Uses get_rand_chr to generate a character that conforms the requirements set
// by the argument flags.
char get_valid_chr(const char *illegal) {
  char c = get_rand_chr();

  while (strchr(illegal, c) != NULL)
    c = get_rand_chr();

  return c;
}

char * get_illegal_chars() {
  char* illegal = malloc(94 * sizeof(char));
  memset(illegal, 0, 94 * sizeof(char));
  if (toys.optflags & FLAG(A))
    strcat(illegal, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  if (toys.optflags & FLAG(0))
    strcat(illegal, "1234567890");
  if (toys.optflags & FLAG(B))
    strcat(illegal, "0O1lI'`.,");
  if (toys.optflags & FLAG(v))
    strcat(illegal, "ieaouIEAOU");
  if (!(toys.optflags & FLAG(y)))
    strcat(illegal, "!@#$%^&*()-_=+`~{}[];:'\",.<>/?\\|");
  if (TT.r){
    strcat(illegal, TT.r);
  }
  return illegal;
}

// Generate a new password of length l.
char * get_pw(const unsigned long l, const char *illegal) {
  char *pw = malloc((l+1) * sizeof(char));
  memset(pw, 0, (l+1) * sizeof(char));  // zero out memory for new password.
  unsigned long i;
  for (i = 0; i < l; i++){
    pw[i] = get_valid_chr(illegal);
  }

  return pw;
}

void pwgen_main(void)
{
  // seed the (pseudo) RNG.
  time_t t;
  srand((unsigned) time(&t));

  int length = 8;
  int count = 160;
  int num_columns = 8;
  if (toys.optc > 0) {
    length = atoi(toys.optargs[0]);
    num_columns = 80 / (length+1);
    if (toys.optc > 1) {
      count = atoi(toys.optargs[1]);
    } else if (toys.optflags & FLAG(1)) {
      count = 1;
    } else {
      count = 20 * num_columns;
    }
  }

  char *illegal = get_illegal_chars();
  char **passwords = malloc(count * sizeof(char*));
  int c;
  for (c = 0; c < count; c++)
      passwords[c] = get_pw(length, illegal);

  if (toys.optflags & FLAG(1)){
    for (c = 0; c < count; c++)
      xprintf("%s\n", passwords[c]);
  } else {
    int col;
    c = 0;
    while(c < count){
      for (col = 0; c < count && col < num_columns; col++){
        xprintf("%s ", passwords[c]);
        c += 1;
      }
      xprintf("\n");
    }
  }

  for (c = 0; c < count; c++)
    free(passwords[c]);
  free(passwords);
  free(illegal);
}
