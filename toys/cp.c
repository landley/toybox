/* vi: set sw=4 ts=4:
 *
 * cp.c - Copy files.
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/cp.html
 *
 * "R+ra+d+p+r"
USE_CP(NEWTOY(cp, "<2vslrR+rdpa+d+p+rHLPif", TOYFLAG_BIN))

config CP
	bool "cp"
	default y
	help
	  usage: cp -fiprdal SOURCE... DEST

	  Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
	  be a directory.

		-f	force copy by deleting destination file
		-i	interactive, prompt before overwriting existing DEST
		-p	preserve timestamps, ownership, and permissions
		-r	recurse into subdirectories (DEST must be a directory)
		-d	don't dereference symlinks
		-a	same as -dpr
		-l	hard link instead of copying
		-v	verbose
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
#define FLAG_l 1024	// todo
#define FLAG_s 2048	// todo
#define FLAG_v 4098

DEFINE_GLOBALS(
	char *destname;
	int destisdir;
	int destisnew;
	int keep_symlinks;
)

#define TT this.cp

// Copy an individual file or directory to target.

void cp_file(char *src, char *dst, struct stat *srcst)
{
	int fdout = -1;

	if (toys.optflags & FLAG_v)
		printf("'%s' -> '%s'\n", src, dst);

	// Copy directory or file to destination.

	if (S_ISDIR(srcst->st_mode)) {
		struct stat st2;

		// Always make directory writeable to us, so we can create files in it.
		//
		// Yes, there's a race window between mkdir() and open() so it's
		// possible that -p can be made to chown a directory other than the one
		// we created.  The closest we can do to closing this is make sure
		// that what we open _is_ a directory rather than something else.

		if ((mkdir(dst, srcst->st_mode | 0200) && errno != EEXIST)
			|| 0>(fdout=open(dst, 0)) || fstat(fdout, &st2)
			|| !S_ISDIR(st2.st_mode))
		{
			perror_exit("mkdir '%s'", dst);
		}
	} else if (TT.keep_symlinks && S_ISLNK(srcst->st_mode)) {
		char *link = xreadlink(src);

		// Note: -p currently has no effect on symlinks.  How do you get a
		// filehandle to them?  O_NOFOLLOW causes the open to fail.
		if (!link || symlink(link, dst)) perror_msg("link '%s'", dst);
		free(link);
		return;
	} else if (toys.optflags & FLAG_l) {
		if (link(src, dst)) perror_msg("link '%s'");
		return;
	} else {
		int fdin, i;

		fdin = xopen(src, O_RDONLY);
		for (i=2 ; i; i--) {
			fdout = open(dst, O_RDWR|O_CREAT|O_TRUNC, srcst->st_mode);
			if (fdout>=0 || !(toys.optflags & FLAG_f)) break;
			unlink(dst);
		}
		if (fdout<0) perror_exit("%s", dst);
		xsendfile(fdin, fdout);
		close(fdin);
	}

	// Inability to set these isn't fatal, some require root access.
	// Can't do fchmod() etc here because -p works on mkdir, too.

	if (toys.optflags & FLAG_p) {
		int mask = umask(0), ignored;
		struct utimbuf ut;

		ignored = fchown(fdout,srcst->st_uid, srcst->st_gid);
		ut.actime = srcst->st_atime;
		ut.modtime = srcst->st_mtime;
		utime(dst, &ut);
		umask(mask);
	}
	xclose(fdout);
}

// Callback from dirtree_read() for each file/directory under a source dir.

int cp_node(char *path, struct dirtree *node)
{
	char *s = path+strlen(path);
	struct dirtree *n;

	// Find appropriate chunk of path for destination.

	n = node;
	if (!TT.destisdir) n = n->parent;
	for (;;n = n->parent) {
		while (s!=path) {
			if (*(--s)=='/') break;
		}
		if (!n) break;
	}
	if (s != path) s++;

	s = xmsprintf("%s/%s", TT.destname, s);
	cp_file(path, s, &(node->st));
	free(s);

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
		char *dst;

		// Skip src==dest (should check inodes to catch "cp blah ./blah").

		if (!strcmp(src, TT.destname)) continue;

		// Skip nonexistent sources.

		TT.keep_symlinks = toys.optflags & FLAG_d;
		if (TT.keep_symlinks ? lstat(src, &st) : stat(src, &st))
		{
			perror_msg("'%s'", src);
			toys.exitval = 1;
			continue;
		}

		// Copy directory or file.

		if (TT.destisdir) {
			dst = strrchr(src, '/');
			if (dst) dst++;
			else dst=src;
			dst = xmsprintf("%s/%s", TT.destname, dst);
		} else dst = TT.destname;
		if (S_ISDIR(st.st_mode)) {
			if (toys.optflags & FLAG_r) {
				cp_file(src, dst, &st);

				TT.keep_symlinks++;
				strncpy(toybuf, src, sizeof(toybuf)-1);
				toybuf[sizeof(toybuf)-1]=0;
				dirtree_read(toybuf, NULL, cp_node);
			} else error_msg("Skipped dir '%s'", src);
		} else cp_file(src, dst, &st);
		if (TT.destisdir) free(dst);
	}

	return;

error_notdir:
	error_exit("'%s' isn't a directory", TT.destname);
}
