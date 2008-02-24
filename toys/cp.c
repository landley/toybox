/* vi: set sw=4 ts=4:
 *
 * cp.c - Copy files.
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/cp.html
 *
 * "R+ra+d+p+r"
USE_CP(NEWTOY(cp, "<2slrR+rdpa+d+p+rHLPif", TOYFLAG_BIN))

config CP
	bool "cp"
	default n
	help
	  usage: cp -fpr SOURCE... DEST

	  Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
	  be a directory.

		-f	force copy by deleting destination file
		-i	interactive, prompt before overwriting existing DEST
		-p	preserve timestamps, ownership, and permissions
		-r	recurse into subdirectories (DEST must be a directory)
*/

#include "toys.h"

#define FLAG_f 1
#define FLAG_i 2	// todo
#define FLAG_P 4	// todo
#define FLAG_L 8	// todo
#define FLAG_H 16	// todo
#define FLAG_a 32
#define FLAG_p 64
#define FLAG_d 128	// todo
#define FLAG_R 256
#define FLAG_r 512
#define FLAG_s 1024	// todo
#define FLAG_l 2048	// todo

DEFINE_GLOBALS(
	char *destname;
	int destisdir;
	int destisnew;
)

#define TT this.cp

// Copy an individual file or directory to target.

void cp_file(char *src, struct stat *srcst, int depth)
{
	char *s = NULL;
	int fdout = -1;

	// Trim path from name if necessary.

	if (!depth) s = strrchr(src, '/');
	if (s) s++;
	else s=src;

	// Determine location to create new file/directory at.

	if (TT.destisdir || depth) s = xmsprintf("%s/%s", TT.destname, s);
	else s = xstrdup(TT.destname);

	// Copy directory or file to destination.

	if (S_ISDIR(srcst->st_mode)) {
		struct stat st2;

		// Always make directory writeable to us, so we can create files in it.
		//
		// Yes, there's a race window between mkdir() and open() so it's
		// possible that -p can be made to chown a directory other than the one
		// we created.  The closest we can do to closing this is make sure
		// that what we open _is_ a directory rather than something else.

		if (mkdir(s, srcst->st_mode | 0200) || 0>(fdout=open(s, 0))
			|| fstat(fdout, &st2) || !S_ISDIR(st2.st_mode))
		{
			perror_exit("mkdir '%s'", s);
		}
	} else if ((depth || (toys.optflags & FLAG_d)) && S_ISLNK(srcst->st_mode)) {
		struct stat st2;
		char *link = xreadlink(src);

		// Note: -p currently has no effect on symlinks.  How do you get a
		// filehandle to them?  O_NOFOLLOW causes the open to fail.
		if (!link || symlink(link, s)) perror_msg("link '%s'",s);
		free(link);
	} else if (toys.optflags & FLAG_l) {
		if (link(src, s)) perror_msg("link '%s'");
	} else {
		int fdin, i;

		fdin = xopen(src, O_RDONLY);
		for (i=2 ; i; i--) {
			fdout = open(s, O_RDWR|O_CREAT|O_TRUNC, srcst->st_mode);
			if (fdout>=0 || !(toys.optflags & FLAG_f)) break;
			unlink(s);
		}
		if (fdout<0) perror_exit("%s", s);
		xsendfile(fdin, fdout);
		close(fdin);
	}

	// Inability to set these isn't fatal, some require root access.
	// Can't do fchmod() etc here because -p works on mkdir, too.

	if (toys.optflags & FLAG_p) {
		int mask = umask(0);
		struct utimbuf ut;

		fchown(fdout,srcst->st_uid, srcst->st_gid);
		ut.actime = srcst->st_atime;
		ut.modtime = srcst->st_mtime;
		utime(s, &ut);
		umask(mask);
	}
	xclose(fdout);
	free(s);
}

// Callback from dirtree_read() for each file/directory under a source dir.

int cp_node(char *path, struct dirtree *node)
{
	char *s = path+strlen(path);
	struct dirtree *n = node;
	int depth = 0;

	// Find appropriate chunk of path for destination.

	for (;;) {
		if (*(--s) == '/') {
			depth++;
			if (!n->parent) break;
			n = n->parent;
		}
	}
	s++;
		
	cp_file(s, &(node->st), depth);
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
		TT.destisnew++;

	// If destination exists...

	} else {
		if (S_ISDIR(st.st_mode)) TT.destisdir++;
		else if (toys.optc > 1) goto error_notdir;
	}

	// Handle sources

	for (i=0; i<toys.optc; i++) {
		char *src = toys.optargs[i];

		// Skip nonexistent sources, or src==dest.

		if (!strcmp(src, TT.destname)) continue;
		if ((toys.optflags & FLAG_d) ? lstat(src, &st) : stat(src, &st))
		{
			perror_msg("'%s'", src);
			toys.exitval = 1;
			continue;
		}

		// Copy directory or file.

		if (S_ISDIR(st.st_mode)) {
			if (toys.optflags & FLAG_r) {
				cp_file(src, &st, 0);
				strncpy(toybuf, src, sizeof(toybuf)-1);
				toybuf[sizeof(toybuf)-1]=0;
				dirtree_read(toybuf, NULL, cp_node);
			} else error_msg("Skipped dir '%s'", src);
		} else cp_file(src, &st, 0);
	}

	return;

error_notdir:
	error_exit("'%s' isn't a directory", TT.destname);
}
