/* vi: set sw=4 ts=4:
 *
 * ls.c - list files
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ls.html

USE_LS(NEWTOY(ls, "AnRlF1a", TOYFLAG_BIN))

config LS
	bool "ls"
	default n
	help
	  usage: ls [-lFaA1] [directory...]
	  list files

          -1    list one file per line
          -a    list all files
	  -A	list all files except . and ..
          -F    append a character as a file type indicator
          -l    show full details for each file
*/

#include "toys.h"

#define FLAG_a 1
#define FLAG_1 2
#define FLAG_F 4
#define FLAG_l 8
#define FLAG_R 16
#define FLAG_n 32
#define FLAG_A 64

static int dir_filter(const struct dirent *d)
{
    /* Skip over all '.*' entries, unless -a is given */
    if (!(toys.optflags & FLAG_a)) {
        /* -A means show everything except the . & .. entries */
        if (toys.optflags & FLAG_A) {
            if (strcmp(d->d_name, ".") == 0 ||
                strcmp(d->d_name, "..") == 0)
                return 0;
        } else if (d->d_name[0] == '.')
            return 0;
    }
    return 1;
}

static void do_ls(int fd, char *name)
{
    struct dirent **entries;
    int nentries;
    int i;
    int maxwidth = -1;
    int ncolumns = 1;
    struct dirent file_dirent;
    struct dirent *file_direntp;

    if (!name || strcmp(name, "-") == 0)
        name = ".";

    if (toys.optflags & FLAG_R)
        xprintf("\n%s:\n", name);

    /* Get all the files in this directory */
    nentries = scandir(name, &entries, dir_filter, alphasort);
    if (nentries < 0) {
        /* We've just selected a single file, so create a single-length list */
        /* FIXME: This means that ls *.x results in a whole bunch of single
         * listings, not one combined listing.
         */
        if (errno == ENOTDIR) {
            nentries = 1;
            strcpy(file_dirent.d_name, name);
            file_direntp = &file_dirent;
            entries = &file_direntp;
        } else 
            perror_exit("ls: cannot access %s'", name);
    }


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
        ncolumns = maxwidth ? columns / maxwidth : 1;
    }

    for (i = 0; i < nentries; i++) {
        struct dirent *ent = entries[i];
        int len = strlen(ent->d_name);
        struct stat st;
        int stat_valid = 0;

        sprintf(toybuf, "%s/%s", name, ent->d_name);

        /* Provide the ls -l long output */
        if (toys.optflags & FLAG_l) {
            char type;
            char timestamp[64];
            struct tm mtime;

            if (lstat(toybuf, &st))
                perror_exit("Can't stat %s", toybuf);
            stat_valid = 1;
            if (S_ISDIR(st.st_mode))
                type = 'd';
            else if (S_ISCHR(st.st_mode))
                type = 'c';
            else if (S_ISBLK(st.st_mode))
                type = 'b';
            else if (S_ISLNK(st.st_mode))
                type = 'l';
            else
                type = '-';

            xprintf("%c%c%c%c%c%c%c%c%c%c ", type,
                (st.st_mode & S_IRUSR) ? 'r' : '-',
                (st.st_mode & S_IWUSR) ? 'w' : '-',
                (st.st_mode & S_IXUSR) ? 'x' : '-',
                (st.st_mode & S_IRGRP) ? 'r' : '-',
                (st.st_mode & S_IWGRP) ? 'w' : '-',
                (st.st_mode & S_IXGRP) ? 'x' : '-',
                (st.st_mode & S_IROTH) ? 'r' : '-',
                (st.st_mode & S_IWOTH) ? 'w' : '-',
                (st.st_mode & S_IXOTH) ? 'x' : '-');

            xprintf("%2d ", st.st_nlink);
            if (toys.optflags & FLAG_n) {
                xprintf("%4d ", st.st_uid);
                xprintf("%4d ", st.st_gid);
            } else {
                struct passwd *pwd = getpwuid(st.st_uid);
                struct group *grp = getgrgid(st.st_gid);
                if (!pwd)
                    xprintf("%4d ", st.st_uid);
                else
                    xprintf("%-10s ", pwd->pw_name);
                if (!grp)
                    xprintf("%4d ", st.st_gid);
                else
                    xprintf("%-10s ", grp->gr_name);
            }
            if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
                xprintf("%3d, %3d ", major(st.st_rdev), minor(st.st_rdev));
            else
                xprintf("%12lld ", st.st_size);

            localtime_r(&st.st_mtime, &mtime);

            strftime(timestamp, sizeof(timestamp), "%b %e %H:%M", &mtime);
            xprintf("%s ", timestamp);
        }

        xprintf("%s", ent->d_name);

        /* Append the file-type indicator character */
        if (toys.optflags & FLAG_F) {
            if (!stat_valid) {
                if (lstat(toybuf, &st))
                    perror_exit("Can't stat %s", toybuf);
                stat_valid = 1;
            }
            if (S_ISDIR(st.st_mode)) {
                xprintf("/");
                len++;
            } else if (S_ISREG(st.st_mode) &&
                (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                xprintf("*");
                len++;
            } else if (S_ISLNK(st.st_mode)) {
                xprintf("@");
                len++;
            }
        }
        if (toys.optflags & FLAG_1) {
            xprintf("\n");
        } else {
            if (i % ncolumns == ncolumns - 1)
                xprintf("\n");
            else
                xprintf("%*s", maxwidth - len, "");
        }
    }
    /* Make sure we put at a trailing new line in */
    if (!(toys.optflags & FLAG_1) && (i % ncolumns))
        xprintf("\n");

    if (toys.optflags & FLAG_R) {
        for (i = 0; i < nentries; i++) {
            struct dirent *ent = entries[i];
            struct stat st;
            char dirname[PATH_MAX];

            sprintf(dirname, "%s/%s", name, ent->d_name);
            if (lstat(dirname, &st))
                perror_exit("Can't stat %s", dirname);
            if (S_ISDIR(st.st_mode))
                do_ls(0, dirname);
        }
    }
}

void ls_main(void)
{
    /* If the output is not a TTY, then just do one-file per line
     * This makes ls easier to use with other command line tools (grep/awk etc...)
     */
    if (!isatty(fileno(stdout)))
        toys.optflags |= FLAG_1;
    /* Long output must be one-file per line */
    if (toys.optflags & FLAG_l)
        toys.optflags |= FLAG_1;
    loopfiles(toys.optargs, do_ls);
}
