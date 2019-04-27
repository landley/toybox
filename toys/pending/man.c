/* man.c - Read system documentation
 *
 * Copyright 2019 makepost <makepost@firemail.cc>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/man.html

USE_MAN(NEWTOY(man, "<1>2k:M:", TOYFLAG_USR|TOYFLAG_BIN))

config MAN
  bool "man"
  default n
  help
    usage: man [-k STRING] | [SECTION] COMMAND

    Read manual page for system command.

    -k	Search short 

    Man pages are divided into 8 sections, each with an info page (man 8 info).
    1) executables, 2) syscalls, 3) library functions, 4) /dev files,
    5) file formats (ala /etc/hosts), 6) games, 7) miscelanous, 8) sysadmin

    If you don't specify a section it'll show the lowest numbered one,
    but "man 1 mkdir" and "man 2 mkdir" are different things.

    The shell builtins don't have section 1 man pages, see the "help" command.
*/

#define FOR_man
#include <toys.h>
#include <glob.h>

GLOBALS(
  char *M, *k;

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

static void do_man(char **pline, long len)
{
  char *line;

  if (!pline) {
    newln();
    return;
  }
  line = *pline;

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
    else {
      if (TT.cell) TT.cell++;
      put(" ");
      put(TT.line);
    }
}

// Try opening all the possible file extensions.
int tryfile(char *section, char *name)
{
  char *suf[] = {".gz", ".bz2", ".xz", ""}, *end,
    *s = xmprintf("%s/man%s/%s.%s.bz2", TT.M, section, name, section);
  int fd, i, and = 1;

  end = s+strlen(s);
  do {
    for (i = 0; i<ARRAY_LEN(suf); i++) {
      strcpy(end-4, suf[i]);
      if ((fd = open(s, O_RDONLY))!= -1) {
        if (*suf[i]) {
          int fds[] = {fd, -1};

          sprintf(toybuf, "%czcat"+(2*!i), suf[i][1]);
          xpopen_both((char *[]){toybuf, s, 0}, fds);
          fd = fds[1];
        }
        goto done;
      }
    }
    end -= strlen(section)+1;
  } while (and--);

done:
  free(s);
  return fd;
}

void man_main(void)
{
  char *order = "18325467";
  int fd;

  if (!TT.M) TT.M = "/usr/share/man";

  if (!toys.optc || FLAG(k)) error_exit("not yet");

  if (toys.optc == 1) {
    if (strchr(*toys.optargs, '/')) fd = xopen(*toys.optargs, O_RDONLY);
    else for (order = "18325467"; *order; order++) {
      *toybuf = *order;
      if (-1 != (fd = tryfile(toybuf, *toys.optargs))) break;
    }
    if (!*order) error_exit("no %s", *toys.optargs);

  // If they specified a section, look for file in that section
  } else if (-1 == (fd = tryfile(toys.optargs[0], toys.optargs[1])))
    error_exit("section %s no %s", toys.optargs[0], toys.optargs[1]);

  do_lines(fd, '\n', do_man);
}
