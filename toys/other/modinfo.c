/* modinfo.c - Display module info
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>

USE_MODINFO(NEWTOY(modinfo, "<1F:0", TOYFLAG_BIN))

config MODINFO
  bool "modinfo"
  default y
  help
    usage: modinfo [-0] [-F field] [modulename...]
*/

#define FOR_modinfo
#include "toys.h"

GLOBALS(
  char *field;

  long mod;
)

static char *modinfo_tags[] = {
  "alias", "license", "description", "author", "firmware",
  "vermagic", "srcversion", "intree", "parm", "depends",
};

static void output_field(char *field, char *value)
{
  int len;

  if (TT.field && strcmp(TT.field, field)) return;

  len = strlen(field);

  if (TT.field) xprintf("%s", value);
  else xprintf("%s:%*s%s", field, 15 - len, "", value);
  xputc((toys.optflags & FLAG_0) ? 0 : '\n');
}

static void modinfo_file(struct dirtree *dir)
{
  int fd, len, i;
  char *buf, *pos, *full_name;

  full_name = dirtree_path(dir, NULL);
  output_field("filename", full_name);
  fd = xopen(full_name, O_RDONLY);
  free(full_name);

  len = fdlength(fd);
  if (!(buf = mmap(0, len, PROT_READ, MAP_SHARED, fd, 0)))
    perror_exit("mmap %s", full_name);

  for (pos = buf; pos < buf+len; pos++) {
    if (*pos) continue;

    for (i = 0; i < sizeof(modinfo_tags) / sizeof(*modinfo_tags); i++) {
      char *str = modinfo_tags[i];
      int len = strlen(str);

      if (!strncmp(pos+1, str, len) && pos[len+1] == '=') 
        output_field(str, pos+len+2);
    }
  }

  munmap(buf, len);
  close(fd);
}

static int check_module(struct dirtree *new)
{
  if (S_ISREG(new->st.st_mode)) {
    char *s;

    for (s = toys.optargs[TT.mod]; *s; s++) {
      int len = 0;

      // The kernel treats - and _ the same, so we should too.
      for (len = 0; s[len]; len++) {
        if (s[len] == '-' && new->name[len] == '_') continue;
        if (s[len] == '_' && new->name[len] == '-') continue;
        if (s[len] != new->name[len]) break;
      }
      if (s[len] || strcmp(new->name+len, ".ko")) break;

      modinfo_file(new);
    }
  }

  return dirtree_notdotdot(new);
}

void modinfo_main(void)
{
  struct utsname uts;

  if (uname(&uts) < 0) perror_exit("bad uname");
  sprintf(toybuf, "/lib/modules/%s", uts.release);

  for(TT.mod = 0; TT.mod<toys.optc; TT.mod++) {
    dirtree_read(toybuf, check_module);
  }
}
