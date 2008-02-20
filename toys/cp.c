/* vi: set sw=4 ts=4:
 *
 * cp.c - Copy files.
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/cp.html
 *
 * "R+ra+d+p+r"
USE_HELLO(NEWTOY(hello, "<2rR+rdpa+d+p+rHLPif", TOYFLAG_BIN|TOYFLAG_UMASK))

config CP
	bool "cp"
	default n
	help
	  usage: cp -f SOURCE... DEST

	  Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
	  be a directory.

		-f	force copy by deleting destination file
		-i	interactive, prompt before overwriting existing DEST
		-p	preserve timestamps, ownership, and permissions
		-r	recurse into subdirectories (DEST must be a directory)
*/

#include "toys.h"

#define FLAG_f 1
#define FLAG_i 2
#define FLAG_P 4
#define FLAG_L 8
#define FLAG_H 16
#define FLAG_a 32
#define FLAG_p 64
#define FLAG_d 128
#define FLAG_R 256
#define FLAG_r 512

DEFINE_GLOBALS(
	char *destname;
	int destisdir;
)

#define TT this.cp

// Copy an individual file or directory to target.

void cp_file(char *src, struct stat *srcst, int topdir, int again)
{
	char *s = NULL;
	int mode = (toys.optflags & FLAG_p) ? 0700 : 0777;

	// The second time we're called, chmod data.  We can't do this on
	// the first pass because we may copy files into a read-only directory.
	if (again) {
		if (toys.optflags & FLAG_p) {
			struct utimbuf ut;

			// Inability to set these isn't fatal, some require root access.
			// Can't do fchmod() etc here because -p works on mkdir, too.
			chown(s, srcst->st_uid, srcst->st_gid);
			chmod(s, srcst->st_mode);
			ut.actime = srcst->st_atime;
			ut.modtime = srcst->st_mtime;
			utime(s, &ut);
		}
		return;
	}

	// Trim path from name if necessary.
	if (topdir) s = strrchr(src, '/');
	if (!s) s=src;

	// Determine location to create new file/directory at.
	if (TT.destisdir) s = xmsprintf(toybuf, "%s/%s", TT.destname, s);
	else s = xstrdup(TT.destname);

	// Copy directory or file to destination.
	if (S_ISDIR(srcst->st_mode)) {
		if (mkdir(s, mode)) perror_exit("mkdir '%s'", s);
	} else {
		int fdin, fdout;
		fdin = xopen(src, O_RDONLY);
		fdout = xcreate(s, O_CREAT|O_TRUNC, mode);
		xsendfile(fdin, fdout);
		close(fdin);
		xclose(fdout);
	}
}

// Callback from dirtree_read() for each file/directory under a source dir.

int cp_node(struct dirtree *node, int after)
{
	cp_file(node->name, &(node->st), 0, after);
	return 0;
}

void cp_main(void)
{
	struct stat st;
	int i;

	// Grab target argument.  (Guaranteed to be there due to "<2" above.)

	TT.destname = toys.optargs[--toys.optc];

	// If destination doesn't exist, are we ok with that?
	if (stat(TT.destname, &st)) {
		if (toys.optc>1) goto error_notdir;

	// If destination exists...
	} else {
		if (S_ISDIR(st.st_mode)) TT.destisdir++;
		else if (toys.optc > 1) goto error_notdir;
	}

	// Handle sources
	for (i=0; i<toys.optc; i++) {
		char *src = toys.optargs[i];

		// Skip nonexistent sources...
		if (!((toys.optflags & FLAG_d) ? lstat(src, &st) : stat(src, &st))) {
			perror_msg("'%s'", src);
			toys.exitval = 1;
			continue;
		}

		// Copy directory or file.
		if (S_ISDIR(st.st_mode)) {
			if (toys.optflags & FLAG_r) {
				cp_file(src, &st, 1, 0);
				dirtree_read(src, NULL, cp_node);
				cp_file(src, &st, 1, 1);
			} else error_msg("Skipped dir '%s'", src);
		} else {
			cp_file(src, &st, 1, 0);
			cp_file(src, &st, 1, 1);
		}
	}
	return;

error_notdir:
	error_exit("'%s' isn't a directory", TT.destname);
}
