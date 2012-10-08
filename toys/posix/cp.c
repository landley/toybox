/* vi: set sw=4 ts=4:
 *
 * cp.c - Copy files.
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cp.html
 *
 * TODO: "R+ra+d+p+r" sHLPR

USE_CP(NEWTOY(cp, "<2"USE_CP_MORE("rdavsl")"RHLPfip", TOYFLAG_BIN))

config CP
	bool "cp (broken by dirtree changes)"
	default n
	help
		usage: cp [-fipRHLP] SOURCE... DEST

		Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
		be a directory.

		-f	force copy by deleting destination file
		-i	interactive, prompt before overwriting existing DEST
		-p	preserve timestamps, ownership, and permissions
		-R	recurse into subdirectories (DEST must be a directory)
		-H	Follow symlinks listed on command line
		-L	Follow all symlinks
		-P	Do not follow symlinks [default]

config CP_MORE
	bool "cp -rdavsl options"
	default y
	depends on CP
	help
		usage: cp [-rdavsl]

		-r	synonym for -R
		-d	don't dereference symlinks
		-a	same as -dpr
		-l	hard link instead of copy
		-s	symlink instead of copy
		-v	verbose
*/

#define FOR_cp
#include "toys.h"

// TODO: PLHlsd

GLOBALS(
	char *destname;
	int destisdir;
	int keep_symlinks;
)

// Copy an individual file or directory to target.

void cp_file(char *src, char *dst, struct stat *srcst)
{
	int fdout = -1;

	// -i flag is specified and dst file exists.
	if ((toys.optflags&FLAG_i) && !access(dst, R_OK)
		&& !yesno("cp: overwrite", 1))
			return;

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

	if (toys.optflags & (FLAG_p|FLAG_a)) {
		int mask = umask(0);
		struct utimbuf ut;

		(void) fchown(fdout,srcst->st_uid, srcst->st_gid);
		ut.actime = srcst->st_atime;
		ut.modtime = srcst->st_mtime;
		utime(dst, &ut);
		umask(mask);
	}
	xclose(fdout);
}

// Callback from dirtree_read() for each file/directory under a source dir.

int cp_node(struct dirtree *node)
{
	char *path = dirtree_path(node, 0); // TODO: use openat() instead
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
	free(path); // redo this whole darn function.

	return 0;
}

void cp_main(void)
{
	char *dpath = NULL;
	struct stat st, std;
	int i;

	// Identify destination

	if (!stat(TT.destname, &std) && S_ISDIR(std.st_mode)) TT.destisdir++;
	else if (toys.optc>1) error_exit("'%s' not directory", TT.destname);

   // TODO: This is too early: we haven't created it yet if we need to
	if (toys.optflags & (FLAG_R|FLAG_r|FLAG_a))
		dpath = realpath(TT.destname = toys.optargs[--toys.optc], NULL);

	// Loop through sources

	for (i=0; i<toys.optc; i++) {
		char *dst, *src = toys.optargs[i];

		// Skip src==dest (TODO check inodes to catch "cp blah ./blah").

		if (!strncmp(src, TT.destname)) continue;

		// Skip nonexistent sources.

		TT.keep_symlinks = toys.optflags & (FLAG_d|FLAG_a);
		if (TT.keep_symlinks ? lstat(src, &st) : stat(src, &st)
			|| (st.st_dev = dst.st_dev && st.st_ino == dst.dst_ino))
		{
objection:
			perror_msg("bad '%s'", src);
			toys.exitval = 1;
			continue;
		}

		// Copy directory or file.

		if (TT.destisdir) {
			char *s;

			// Catch "cp -R .. ." and friends that would go on forever
			if (dpath && (s = realpath(src, NULL)) {
				int i = strlen(s);
				i = (!strncmp(s, dst, i) && (!s[i] || s[i]=='/'));
				free(s);

				if (i) goto objection;
			}

			// Create destination filename within directory
			dst = strrchr(src, '/');
			if (dst) dst++;
			else dst=src;
			dst = xmsprintf("%s/%s", TT.destname, dst);
		} else dst = TT.destname;

		if (S_ISDIR(st.st_mode)) {
			if (toys.optflags & (FLAG_r|FLAG_R|FLAG_a)) {
				cp_file(src, dst, &st);

				TT.keep_symlinks++;
				dirtree_read(src, cp_node);
			} else error_msg("Skipped dir '%s'", src);
		} else cp_file(src, dst, &st);
		if (TT.destisdir) free(dst);
	}

	if (CFG_TOYBOX_FREE) free(dpath);
	return;
}
