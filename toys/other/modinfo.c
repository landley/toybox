/* vi: set sw=4 ts=4:
 *
 * modinfo.c - Display module info
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *

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
)

static const char *modinfo_tags[] = {
    "alias", "license", "description", "author", "vermagic",
    "srcversion", "intree", "parm", "depends",
};

static void output_field(const char *field, const char *value)
{
    int len;

    if (TT.field && strcmp(TT.field, field) != 0)
        return;

    len = strlen(field);

    if (TT.field)
        xprintf("%s", value);
    else
        xprintf("%s:%*s%s",
                field, 15 - len, "", value);
    if (toys.optflags & FLAG_0)
        xwrite(fileno(stdout), "\0", 1);
    else
        xputs("");
}


static void modinfo_file(struct dirtree *dir)
{
    int fd, len, i;
    char *buf, *pos;
    char *full_name;

    full_name = dirtree_path(dir, NULL);

    output_field("filename", full_name);
    fd = xopen(full_name, O_RDONLY);
    len = fdlength(fd);
    buf = xmalloc(len);
    xreadall(fd, buf, len);

    for (pos = buf; pos < buf + len + 10; pos++) {
        if (*pos)
            continue;

        for (i = 0; i < sizeof(modinfo_tags) / sizeof(modinfo_tags[0]); i++) {
            const char *str = modinfo_tags[i];
            int len = strlen(str); 
            if (strncmp(pos + 1, str, len) == 0 && pos[len + 1] == '=')
                output_field(str, &pos[len + 2]);
        }
    }

    free(full_name);
    free(buf);
    close(fd);
}

static int check_module(struct dirtree *new)
{
    if (S_ISREG(new->st.st_mode)) {
        char **s;
        for (s = toys.optargs; *s; s++) {
            int len = strlen(*s);
            if (!strncmp(*s, new->name, len) && !strcmp(new->name+len, ".ko"))
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
    dirtree_read(toybuf, check_module);
}
