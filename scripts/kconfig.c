#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
mainmenu, source, comment
config
  bool "string"
  int "string"
  default y/n/SYMBOL
  depends [on] SYMBOL
  help
menu/choice
  prompt "string"
  default, help
  [config...]
endmenu/endchoice

# bool without a string is invisible, no prompt just help text
*/

// Data we're collecting about each entry
struct kconfig {
  struct kconfig *next, *contain;
  char *symbol, *value, *type, *prompt, *def, *depend, *help;
};

// 0 = allnoconfig, 1 = defconfig, 2 = allyesconfig, 4 = randconfig
int cfgtype;

// Skip/remove leading space, quotes, and escapes within quotes
char *trim(char *s)
{
  int len, in, out;

  while (isspace(*s)) s++;
  len = strlen(s);
  if (*s=='\"' && s[len-1]=='\"') {
    s[--len]=0;
    s++;
    for (in = out = 0; in<len; in++) {
      if (s[in]=='\\') in++;
      s[out++] = s[in];
    }
    s[out] = 0;
  }

  return s;
}

// Get trimmed line
char *getrimline(FILE *fp)
{
  char *line = 0, *s;
  size_t size;

  if (1>getline(&line, &size, fp)) return 0;

  // Trim trailing whitespace
  for (s = line+strlen(line); --s>=line;) {
    if (!isspace(*s)) break;
    *s = 0;
  }

  return line;
}

// Check string against 0 terminated array of strings, return 0 if no match
int strany(char *needle, char *haystack[])
{
  int ii;

  for (ii = 0; haystack[ii] && strcmp(needle, haystack[ii]); ii++);

  return haystack[ii] ? ii+1 : 0;
}

// Free *to, assign *to = *from, and zero *from.
void bump(char **to, char **from)
{
  free(*to);
  *to = *from;
  *from = 0;
}

// Read Config.in file, recursing into "source" lines
struct kconfig *read_Config(char *name, struct kconfig *contain)
{
  FILE *fp = fopen(name, "r");
  char *line, *help = 0, *s, *ss, *keywords[] = {"mainmenu", "menu",
    "choice", "comment", "config", "endmenu", "endchoice", 0};
  struct kconfig *kc, *klist = 0;
  int ii, jj, count = 1, hindent;
  size_t size;

  if (!fp) dprintf(2, "Can't open '%s'\n", name), exit(1);
  for (; (s = line = getrimline(fp)); free(line), count++) {
    // Append help text?
    if (help) {
      if (!kc) { fp = 0; break; }
      for (ii = 0; *s==' ' || *s=='\t'; s++) {
        if (*s=='\t') ii |= 7;
        ii++;
        if (*help && hindent<ii) break;
      }
      if (!*help) {
        if (!*s) continue;
        hindent = ii;
      }

      // Does this end the help block?
      jj = strlen(help);
      if (ii<hindent && *s) {
        for (ii = strlen(help); ii--; help[ii]=0) if (!isspace(help[ii])) break;
        kc->help = help;
        help = 0;
      } else {
        help = realloc(help, jj+strlen(s)+2);
        if (jj) help[jj++]='\n';
        strcpy(help+jj, s);

        continue;
      }
    }

    // What's the keyword?
    while (isspace(*s)) s++;
    if (!*s || *s=='#') continue;
    ss = s;
    while (*s && !isspace(*s)) s++;
    ss = strndup(ss, s-ss);

    // Include other file
    if (!strcmp(ss, "source")) {
      struct kconfig *kt;

      if (!(kt = read_Config(trim(s), contain))) { fp = 0; break; }
      if (klist) kc = (kc->next) = kt;
      else klist = kc = kt;
      while (kc->next) kc = kc->next;
    // start help block
    } else if (!strcmp(ss, "help")) help = strdup("");
    // start a new config entry?
    else if ((ii = strany(ss, keywords))) {
      struct kconfig *kt = calloc(sizeof(struct kconfig), 1);

      if (ii>5) contain = kc->contain;
      kt->contain = contain;
      if (ii<4) contain = kt;
      if (klist) kc = (kc->next = kt);
      else klist = kc = kt;
      if (!strcmp(ss, "config")) {
        if (!*s) { fp = 0; break; }
        else kt->symbol = strdup(trim(s));
      } else if (*s) kt->prompt = strdup(trim(s));
      bump(&kt->type, &ss);
    } else if (!kc) { fp = 0; break; }
    else if (!strcmp(ss, "default")) kc->def = strdup(trim(s));
    else if (strany(ss, (char *[]){"bool", "int", "string", "prompt", 0})) {
      if (*ss!='p') bump(&kc->type, &ss);
      ss = strdup(trim(s));
      bump(&kc->prompt, &ss);
    } else if (!strcmp(ss, "depends")) {
      while (isspace(*s)) s++;
      if (s[0]=='o' && s[1]=='n' && isspace(s[2])) s += 3;
      ss = strdup(trim(s));
      bump(&kc->depend, &ss);
    } else { dprintf(2, "%d %s\n", count, line); fp = 0; break; }

    free(ss);
  }

  if (!fp) dprintf(2, "%s[%d]: bad %s\n", name, count, line ? : ""), exit(1);
  fclose(fp);

  return klist;
}

// TODO: randconfig isn't randomizing CHOICE entries
// Return current value of symbol, whether set or default, ignoring dependencies
int value(struct kconfig *kc)
{
  char *s = kc->value ? : kc->def ? : 0;

  if (!s) {
    if (!strcmp(kc->contain->type, "choice"))
      return !strcmp(kc->contain->def, kc->symbol);
    s = "";
  }
  if (*kc->type!='b') return atoi(s);
  if (kc->value) return *kc->value=='y';
  if (cfgtype==1 || !*kc->prompt) return *s=='y';
  if (cfgtype==4) return random()&1;
  return !!cfgtype;
}

// Find symbol by name
struct kconfig *lookup(struct kconfig *klist, char *symbol)
{
  for (;klist; klist = klist->next)
    if (klist->symbol && !strcmp(klist->symbol, symbol)) break;

  return klist;
}

// TODO: parentheses, select
// Return value of symbol as masked by dependencies
int depends(struct kconfig *klist, struct kconfig *kc)
{
  struct kconfig *kt;
  char *ss, *tt, *sym;
  int rc = 1, flip;

  if (kc->depend) for (ss = kc->depend; *ss;) {
    for (tt = ss; *tt && !isspace(*tt);) tt++;
    if (tt==ss) break;
    ss = sym = strndup(ss, tt-ss);
    sym += flip = *sym=='!';
    while (isspace(*tt)) tt++;
    if (!strcmp("&&", sym)) {
      if (!rc) return 0;
    } else if (!strcmp("||", sym)) {
      if (rc) return 1;
    } else if (!(kt = lookup(klist, sym))) {
      dprintf(2, "unknown dependency %s in %s\n", sym, kc->symbol);
      exit(1);
    } else {
      rc = depends(kc, kt) ? value(kt) : 0;
      if (flip) rc = !rc;
    }
    free(ss);
    ss = tt;
  }

  return rc;
}


// Set values for symbols
void read_dotconfig(struct kconfig *klist, FILE *fp)
{
  char *line, *s, *name, *val;
  struct kconfig *kk;

  for (; (line = getrimline(fp)); free(line)) {
    // kconfig has a crazy way to say SYMBOL=n
    if (!strncmp(line, "# CONFIG_", 9) && (s = strchr(line, ' '))
      && !strcmp(s, " is not set\n"))
    {
      *s = 0;
      name = line+9;
      val = "n";
    } else if (!strncmp(line, "CONFIG_", 7) && (s = strchr(line, '='))) {
      *s = 0;
      name=line+7;
      val = s+1;
    } else {
      for (s = line; isspace(*s); s++);
      if (*s && *s!='#') dprintf(2, "bad line %s\n", line);
      continue;
    }
    if (!(kk = lookup(klist, name))) dprintf(2, "bad symbol %s\n", name);
    else {
// TODO   if (!strcmp(kc->contain->type, "choice"))

      s = strdup(val);
      bump(&kk->value, &s);
    }
  }

  fclose(fp);
}

// Parse command line options, producing various types of output
void options(char *opt)
{
  struct kconfig *kc = read_Config("Config.in", 0), *kk;
  char *ss, *tt, *esc = "\n\\\"";
  FILE *fp;

  if (!strcmp(opt, "-h")) {
    for (kk = kc; kk; kk = kk->next) {
      if (!kk->symbol || !kk->help) continue;
      printf("#define HELP_");
      for (ss = kk->symbol; *ss; ss++) putchar(tolower(*ss));
      printf(" \"");
      for (ss = kk->help; *ss; ss++)
        if (!(tt = strchr(esc, *ss))) putchar(*ss);
        else printf("\\%c", "n\\\""[tt-esc]);
      printf("\"\n\n");
    }

    return;
  }

  if ((ss = getenv("KCONFIG_ALLCONFIG"))) {
    if (!(fp = fopen(ss, "r")))
      exit(dprintf(2, "bad KCONFIG_ALLCONFIG=%s\n", ss)|1);
    read_dotconfig(kc, fp);
  }
// TODO  if ((fp = fopen(getenv("KCONFIG_CONFIG") ? : ".config")))
//    read_dotconfig(kc, fp);

  if (-1 != (cfgtype = strany(opt, (char *[]){"-n", "-d", "-y", 0})-1)) {
    time_t t = time(0);
    struct tm *tt = localtime(&t);
    char buf[64];

    strftime(buf, sizeof(buf), "%c", tt);
    printf("# %s\n\n", buf);

   for (kk = kc; kk; kk = kk->next) {
      if (!strcmp(kk->type, "menu") || !strcmp(kk->type, "comment"))
        printf("\n#\n# %s\n#\n", kk->prompt ? : "");
      if (!(ss = kk->symbol)) continue;
      if (*kk->type=='b') {
        printf((depends(kc, kk) && value(kk))
          ? "CONFIG_%s=y\n" : "# CONFIG_%s is not set\n", ss);
      } else if (*kk->type=='s')
        printf("CONFIG_%s=\"%s\"\n", ss, kk->value ? : kk->def ? : "");
      else printf("CONFIG_%s=%d\n", ss, value(kk));
    }
  }
}

// Read .config file and set each symbol
// void getconfig(struct kconfig *klist, char *) { }


int main(int argc, char *argv[])
{
  if (argc==2) options(argv[1]);

  return 0;
}
