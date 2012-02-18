/* vi: set sw=4 ts=4:
 *
 * pidof.c - Print the PIDs of all processes with the given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * Not in SUSv4.
 * See http://opengroup.org/onlinepubs/9699919799/utilities/

USE_PIDOF(NEWTOY(pidof, "e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))

config PIDOF
	bool "pidof"
	default y
	help
	  usage: pidof [NAME]...

	  Print the PIDs of all processes with the given names.
*/

#include "toys.h"

#define TT this.pidof

#define PATH_LEN 64
#define PROC_DIR "/proc/"
#define CMD_LINE "/cmdline"

static int matched = 0;

static int for_each_pid(void (*callback) (const char *pid)) {
    DIR *dp;
    struct dirent *entry;
    FILE *fp;
    int n, pathpos;
    char cmd[PATH_MAX];
    char path[PATH_LEN];
    char **curname;

    dp = opendir(PROC_DIR);
    if (!dp) {
        perror("opendir");
        return 1;
    }

    while ((entry = readdir(dp))) {
        if (!isdigit(entry->d_name[0])) continue;
        strcpy(path, PROC_DIR);
        pathpos = strlen(PROC_DIR);

        if (pathpos + strlen(entry->d_name) + 1 > PATH_LEN) continue;

        strcpy(&path[pathpos], entry->d_name);
        pathpos += strlen(entry->d_name);

        if (pathpos + strlen(CMD_LINE) + 1 > PATH_LEN) continue;
        strcpy(&path[pathpos], CMD_LINE);

        fp = fopen(path, "r");
        if (!fp) {
            perror("fopen");
            continue;
        }

        n = fread(cmd, 1, PATH_MAX, fp); 
        fclose(fp);
        if (n == 0) continue;

        for (curname = toys.optargs; *curname; curname++) {
            if (strcmp(basename(cmd), *curname) == 0) {
                callback(entry->d_name);
            }
        }
    }

    closedir(dp);

    return 0;
}

static void print_pid (const char *pid) {
    if (matched) putchar(' ');
    fputs(pid, stdout);
    matched = 1;
}

void pidof_main(void)
{
    int err;

    if (!toys.optargs) exit(1);

    err = for_each_pid(print_pid);
    if (err) exit(1);

    if (!matched)
        exit(1);
    else
        putchar('\n');
}
