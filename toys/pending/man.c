/* man.c - Read system documentation
 *
 * Copyright 2019 makepost <makepost@firemail.cc>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/man.html

USE_MAN(NEWTOY(man, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config MAN
  bool "man"
  default n
  help
    usage: man COMMAND

    Read manual for system command.
*/

#define FOR_man
#include <toys.h>
#include <glob.h>

GLOBALS(
  char any, cell, *f, *line;
)

static void newln()
{
  if (TT.any) putchar('\n');
  if (TT.any && TT.cell != 2) putchar('\n'); // gawk alias
  TT.any = TT.cell = 0;
}
static void put(char *x)
{
  while (*x && *x != '\n') TT.any = putchar(*x++);
}

// Substitute with same length or shorter.
static void s(char *x, char *y)
{
  int i = strlen(x), j = strlen(y), k, l;

  for (k = 0; TT.line[k]; k++) if (!strncmp(x, &TT.line[k], i)) {
    memmove(&TT.line[k], y, j);
    for (l = k += j; TT.line[l]; l++) TT.line[l] = TT.line[l + i - j];
  }
}

static char start(char *x)
{
  return !strncmp(x, TT.line, strlen(x));
}

static void trim(char *x)
{
  if (start(x)) while (*x++) TT.line++;
}

static void do_man(FILE *fp)
{
  size_t len = 0;
  char *line = 0;
  while (getline(&line, &len, fp) > 0) {
    TT.line = line;
    s("\\fB", ""), s("\\fI", ""), s("\\fP", ""), s("\\fR", ""); // bash bold,ita
    s("\\(aq", "'"), s("\\(cq", "'"), s("\\(dq", "\""); // bash,rsync quote
    s("\\*(lq", "\""), s("\\*(rq", "\""); // gawk quote
    s("\\(bu", "*"), s("\\(bv", "|"); // bash symbol
    s("\\&", ""), s("\\f(CW", ""); // gawk,rsync fancy
    s("\\-", "-"), s("\\(", ""), s("\\^", ""), s("\\e", "\\"); // bash escape
    s("\\*(", "#"); // gawk var
    if (start(".BR")) trim(".BR "), s(" ", ""); // bash boldpunct
    if (start(".IP")) newln(), trim(".IP "); // bash list
    if (start(".IR")) trim(".IR "), s(" ", ""); // bash itapunct
    trim(".B "); // bash bold
    trim(".BI "); // gawk boldita
    trim(".FN "); // bash filename
    trim(".I "); // bash ita
    trim(".if n "); // bash nroff
    if (start(".PP")) newln(); // bash paragraph
    else if (start(".SM")); // bash small
    else if (start(".S")) newln(), put(TT.line + 4), newln(); // bash section
    else if (start(".so")) put("See "), put(basename(TT.line + 4)); // lastb
    else if (start(".TH")) s("\"", " "), put(TT.line + 4); // gawk,git head
    else if (start(".TP")) newln(), TT.cell = 1; // bash table
    else if (start(".") || start("\'")); // bash,git garbage
    else if (!*TT.line); // emerge
    else ((TT.cell != 0) && TT.cell++), put(" "), put(TT.line);
  }
  newln();
  free(line);
  fclose(fp);
}

static FILE *bzcat()
{
  char cmd[FILENAME_MAX];
  snprintf(cmd, sizeof(cmd), "bzcat %s", TT.f);
  return popen(cmd, "r");
}

static char *find(char *path, int suf)
{
  glob_t g;
  int i;
  size_t len = strlen(*toys.optargs);
  char *name;
  glob(path, 0, 0, &g);
  for (i = 0; !TT.f && i < g.gl_pathc; i++) {
    name = basename(g.gl_pathv[i]);
    if (strlen(name) == len + suf && !strncmp(name, *toys.optargs, len))
      TT.f = strdup(g.gl_pathv[i]);
  }
  globfree(&g);
  return TT.f;
}

void man_main(void)
{
  chdir("/usr/share/man");
  if (find("man?/*.?.bz2", 6)) do_man(bzcat()); // curl_strequal
  else if (find("man?/*.bz2", 4)) do_man(bzcat()); // curl_strequal.3
  else if (find("man?/*.?", 2)) do_man(fopen(TT.f, "r")); // curl_strnequal
  else if (find("man?/*", 0)) do_man(fopen(TT.f, "r")); // curl_strnequal.3
  if (TT.f) free(TT.f);
}
