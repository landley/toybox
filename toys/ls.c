/* vi: set sw=4 ts=4:
 *
 * ls.c - list files
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ls.htm

USE_LS(NEWTOY(ls, "F1a", TOYFLAG_USR|TOYFLAG_BIN))

config LS
	bool "ls"
	default y
	help
	  usage: ls [-F] [-a] [-1] [directory...]
	  list files

          -F    append a character as a file type indicator
          -a    list all files
          -1    list one file per line
*/

#include "toys.h"

#define FLAG_a 1
#define FLAG_1 2
#define FLAG_F 4

static int dir_filter(const struct dirent *d)
{
    /* Skip over the . & .. entries unless -a is given */
    if (!(toys.optflags & FLAG_a))
        if (d->d_name[0] == '.')
            return 0;
    return 1;
}

static void do_ls(int fd, char *name)
{
    struct dirent **entries;
    int nentries;
    int i;
    int maxwidth = -1;
    int ncolumns;

    if (strcmp(name, "-") == 0)
        name = ".";

    nentries = scandir(name, &entries, dir_filter, alphasort);
    if (nentries < 0)
        perror_exit("ls: cannot access %s'", name);


    /* Determine the widest entry so we can flow them properly */
    if (!(toys.optflags & FLAG_1)) {
        int columns;
        char *columns_str;

        for (i = 0; i < nentries; i++) {
            struct dirent *ent = entries[i];
            int width;

            width = strlen(ent->d_name);
            if (width > maxwidth)
                maxwidth = width;
        }
        /* We always want at least a single space for each entry */
        maxwidth++;
        if (toys.optflags & FLAG_F)
            maxwidth++;

        columns_str = getenv("COLUMNS");
        columns = columns_str ? atoi(columns_str) : 80;
        ncolumns = columns / maxwidth;
    }

    for (i = 0; i < nentries; i++) {
        struct dirent *ent = entries[i];
        int len = strlen(ent->d_name);

        printf("%s", ent->d_name);
        if (toys.optflags & FLAG_F) {
            struct stat st;
            if (stat(ent->d_name, &st))
                perror_exit("Can't stat %s", ent->d_name);
            if (S_ISDIR(st.st_mode)) {
                printf("/");
                len++;
            }
            if (S_ISREG(st.st_mode) &&
                (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                printf("*");
                len++;
            }
        }
        if (toys.optflags & FLAG_1)
            printf("\n");
        else {
            if (i % ncolumns == ncolumns - 1)
                printf("\n");
            else
                printf("%*s", maxwidth - len, "");
        }
    }
    /* Make sure we put at a trailing new line in */
    if (!(toys.optflags & FLAG_1) && (i % ncolumns))
        printf("\n");
}

void ls_main(void)
{
    loopfiles(toys.optargs, do_ls);
}
