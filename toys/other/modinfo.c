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

static void output_field(char *field, char *value)
{
  if (!TT.field) xprintf("%s:%*c", field, 15 - strlen(field), ' ');
  else if (!strcmp(TT.field, field)) return;
  xprintf("%s", value);
  xputc((toys.optflags & FLAG_0) ? 0 : '\n');
}

static void modinfo_file(char *full_name)
{
  int fd, len, i;
  char *buf = 0, *pos, *modinfo_tags[] = {
    "alias", "license", "description", "author", "firmware",
    "vermagic", "srcversion", "intree", "parm", "depends",
  };

  if (-1 != (fd = open(full_name, O_RDONLY))) {
    len = fdlength(fd);
    if (!(buf = mmap(0, len, PROT_READ, MAP_SHARED, fd, 0))) close(fd);
  }

  if (!buf) {
    perror_msg("%s", full_name);
    return;
  } 

  output_field("filename", full_name);

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

      modinfo_file(s = dirtree_path(new, NULL));
      free(s);

      return DIRTREE_ABORT;
    }
  }

  return dirtree_notdotdot(new);
}

void modinfo_main(void)
{
  for(TT.mod = 0; TT.mod<toys.optc; TT.mod++) {
    char *s = strstr(toys.optargs[TT.mod], ".ko");

    if (s && !s[3]) modinfo_file(toys.optargs[TT.mod]);
    else {
      struct utsname uts;

      if (uname(&uts) < 0) perror_exit("bad uname");
      sprintf(toybuf, "/lib/modules/%s", uts.release);
      dirtree_read(toybuf, check_module);
    }
  }
}
