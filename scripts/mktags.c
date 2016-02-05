// Process TAGGED_ARRAY() macros to emit TAG_STRING index macros.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char *argv[])
{
  char *tag = 0;
  int idx = 0;

  for (;;) {
    char *line = 0, *s;
    ssize_t len;

    len = getline(&line, &len, stdin);
    if (len<0) break;
    while (len && isspace(line[len-1])) line[--len]=0;

    // Very simple parser: If we haven't got a TAG then first line is TAG.
    // Then look for { followed by "str" (must be on same line, may have
    // more than one per line), for each one emit #define. Current TAG ended
    // by ) at start of line.

    if (!tag) {
      if (!isalpha(*line)) {
        fprintf(stderr, "bad tag %s\n", line);
        exit(1);
      }
      tag = strdup(line);
      idx = 0;

      continue;
    }

    for (s = line; isspace(*s); s++);
    if (*s == ')') tag = 0;
    else for (;;) {
      char *start;

      while (*s && *s != '{') s++;
      while (*s && *s != '"') s++;
      if (!*s) break;

      start = ++s;
      while (*s && *s != '"') {
        if (!isalpha(*s) && !isdigit(*s)) *s = '_';
        s++;
      }
      printf("#define %s_%*.*s %d\n", tag, -40, (int)(s-start), start, idx);
      printf("#define _%s_%*.*s (1%s<<%d)\n", tag, -39, (int)(s-start), start,
        idx>31 ? "LL": "", idx);
      idx++;
    }
    free(line);
  }
}
