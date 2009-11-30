/* vi: set sw=4 ts=4:
 *
 * patch.c - Apply a "universal" diff.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * see http://www.opengroup.org/onlinepubs/009695399/utilities/patch.html
 * (But only does -u, because who still cares about "ed"?)
 *
 * TODO:
 * -b backup
 * -l treat all whitespace as a single space
 * -N ignore already applied
 * -d chdir first
 * -D define wrap #ifdef and #ifndef around changes
 * -o outfile output here instead of in place
 * -r rejectfile write rejected hunks to this file
 *
 * -E remove empty files --remove-empty-files
 * -f force (no questions asked)
 * -F fuzz (number, default 2)
 * [file] which file to patch

USE_PATCH(NEWTOY(patch, "up#i:R", TOYFLAG_USR|TOYFLAG_BIN))

config PATCH
	bool "patch"
	default y
	help
	  usage: patch [-i file] [-p depth] [-Ru]

	  Apply a unified diff to one or more files.

	  -i	Input file (defaults=stdin)
	  -p	number of '/' to strip from start of file paths (default=all)
	  -R	Reverse patch.
	  -u	Ignored (only handles "unified" diffs)

	  This version of patch only handles unified diffs, and only modifies
	  a file when all all hunks to that file apply.  Patch prints failed
	  hunks to stderr, and exits with nonzero status if any hunks fail.

	  A file compared against /dev/null (or with a date <= the epoch) is
	  created/deleted as appropriate.
*/

#include "toys.h"

DEFINE_GLOBALS(
	char *infile;
	long prefix;

	struct double_list *current_hunk;
	long oldline, oldlen, newline, newlen, linenum;
	int context, state, filein, fileout, filepatch, hunknum;
	char *tempname;
)

#define TT this.patch

#define FLAG_REVERSE 1
#define FLAG_PATHLEN 4

// Dispose of a line of input, either by writing it out or discarding it.

// state < 2: just free
// state = 2: write whole line to stderr
// state = 3: write whole line to fileout
// state > 3: write line+1 to fileout when *line != state

static void do_line(void *data)
{
	struct double_list *dlist = (struct double_list *)data;

	if (TT.state>1 && *dlist->data != TT.state)
		fdprintf(TT.state == 2 ? 2 : TT.fileout,
			"%s\n", dlist->data+(TT.state>3 ? 1 : 0));

	free(dlist->data);
	free(data);
}

static void finish_oldfile(void)
{
	if (TT.tempname) replace_tempfile(TT.filein, TT.fileout, &TT.tempname);
	TT.fileout = TT.filein = -1;
}

static void fail_hunk(void)
{
	if (!TT.current_hunk) return;
	TT.current_hunk->prev->next = 0;

	fdprintf(2, "Hunk %d FAILED %ld/%ld.\n", TT.hunknum, TT.oldline, TT.newline);
	toys.exitval = 1;

	// If we got to this point, we've seeked to the end.  Discard changes to
	// this file and advance to next file.

	TT.state = 2;
	llist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;
	delete_tempfile(TT.filein, TT.fileout, &TT.tempname);
	TT.state = 0;
}

static int apply_hunk(void)
{
	struct double_list *plist, *buf = NULL, *check;
	int matcheof = 0, reverse = toys.optflags & FLAG_REVERSE, backwarn = 0;

	// Break doubly linked list so we can use singly linked traversal function.
	TT.current_hunk->prev->next = NULL;

	// Match EOF if there aren't as many ending context lines as beginning
	for (plist = TT.current_hunk; plist; plist = plist->next) {
		if (plist->data[0]==' ') matcheof++;
		else matcheof = 0;
	}
	matcheof = matcheof < TT.context;

	// Loop through input data searching for this hunk.  Match all context
	// lines and all lines to be removed until we've found the end of a
	// complete hunk.
	plist = TT.current_hunk;
	buf = NULL;
	if (TT.context) for (;;) {
		char *data = get_line(TT.filein);

		TT.linenum++;

		// Figure out which line of hunk to compare with next.  (Skip lines
		// of the hunk we'd be adding.)
		while (plist && *plist->data == "+-"[reverse]) {
			if (data && !strcmp(data, plist->data+1)) {
				if (!backwarn) {
					fdprintf(2,"Possibly reversed hunk %d at %ld\n",
						TT.hunknum, TT.linenum);
					backwarn++;
				}
			}
			plist = plist->next;
		}

		// Is this EOF?
		if (!data) {
			// Does this hunk need to match EOF?
			if (!plist && matcheof) break;

			// File ended before we found a place for this hunk.
			fail_hunk();
			goto done;
		}
		check = dlist_add(&buf, data);

		// Compare this line with next expected line of hunk.
		// todo: teach the strcmp() to ignore whitespace.

		// A match can fail because the next line doesn't match, or because
		// we hit the end of a hunk that needed EOF, and this isn't EOF.

		// If match failed, flush first line of buffered data and
		// recheck buffered data for a new match until we find one or run
		// out of buffer.

		for (;;) {
			if (!plist || strcmp(check->data, plist->data+1)) {
				// Match failed.  Write out first line of buffered data and
				// recheck remaining buffered data for a new match.
	
				TT.state = 3;
				check = llist_pop(&buf);
				check->prev->next = buf;
				buf->prev = check->prev;
				do_line(check);
				plist = TT.current_hunk;

				// If we've reached the end of the buffer without confirming a
				// match, read more lines.
				if (check==buf) {
					buf = 0;
					break;
				}
				check = buf;
			} else {
				// This line matches.  Advance plist, detect successful match.
				plist = plist->next;
				if (!plist && !matcheof) goto out;
				check = check->next;
				if (check == buf) break;
			}
		}
	}
out:
	// We have a match.  Emit changed data.
	TT.state = "-+"[reverse];
	llist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;
	TT.state = 1;
done:
	if (buf) {
		buf->prev->next = NULL;
		llist_free(buf, do_line);
	}

	return TT.state;
}

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body

void patch_main(void)
{
	int reverse = toys.optflags & FLAG_REVERSE, state = 0;
	char *oldname = NULL, *newname = NULL;

	if (TT.infile) TT.filepatch = xopen(TT.infile, O_RDONLY);
	TT.filein = TT.fileout = -1;

	// Loop through the lines in the patch
	for(;;) {
		char *patchline;

		patchline = get_line(TT.filepatch);
		if (!patchline) break;

		// Other versions of patch accept damaged patches,
		// so we need to also.
		if (!*patchline) {
			free(patchline);
			patchline = xstrdup(" ");
		}

		// Are we assembling a hunk?
		if (state >= 2) {
			if (*patchline==' ' || *patchline=='+' || *patchline=='-') {
				dlist_add(&TT.current_hunk, patchline);

				if (*patchline != '+') TT.oldlen--;
				if (*patchline != '-') TT.newlen--;

				// Context line?
				if (*patchline==' ' && state==2) TT.context++;
				else state=3;

				// If we've consumed all expected hunk lines, apply the hunk.

				if (!TT.oldlen && !TT.newlen) state = apply_hunk();
				continue;
			}
			fail_hunk();
			state = 0;
			continue;
		}

		// Open a new file?
		if (!strncmp("--- ", patchline, 4) || !strncmp("+++ ", patchline, 4)) {
			char *s, **name = &oldname;
			int i;

			if (*patchline == '+') {
				name = &newname;
				state = 1;
			}

			free(*name);
			finish_oldfile();

			// Trim date from end of filename (if any).  We don't care.
			for (s = patchline+4; *s && *s!='\t'; s++)
				if (*s=='\\' && s[1]) s++;
			i = atoi(s);
			if (i && i<=1970)
				*name = xstrdup("/dev/null");
			else {
				*s = 0;
				*name = xstrdup(patchline+4);
			}

			// We defer actually opening the file because svn produces broken
			// patches that don't signal they want to create a new file the
			// way the patch man page says, so you have to read the first hunk
			// and _guess_.

		// Start a new hunk?
		} else if (state == 1 && !strncmp("@@ -", patchline, 4)) {
			int i;

			i = sscanf(patchline+4, "%ld,%ld +%ld,%ld", &TT.oldline,
						&TT.oldlen, &TT.newline, &TT.newlen);
			if (i != 4)
				error_exit("Corrupt hunk %d at %ld\n", TT.hunknum, TT.linenum);

			TT.context = 0;
			state = 2;

			// If this is the first hunk, open the file.
			if (TT.filein == -1) {
				int oldsum, newsum, del = 0;
				char *s, *name;

 				oldsum = TT.oldline + TT.oldlen;
				newsum = TT.newline + TT.newlen;

				name = reverse ? oldname : newname;

				// We're deleting oldname if new file is /dev/null (before -p)
				// or if new hunk is empty (zero context) after patching
				if (!strcmp(name, "/dev/null") || !(reverse ? oldsum : newsum))
				{
					name = reverse ? newname : oldname;
					del++;
				}

				// handle -p path truncation.
				for (i=0, s = name; *s;) {
					if ((toys.optflags & FLAG_PATHLEN) && TT.prefix == i) break;
					if (*(s++)=='/') {
						name = s;
						i++;
					}
				}

				if (del) {
					printf("removing %s\n", name);
					xunlink(name);
					state = 0;
				// If we've got a file to open, do so.
				} else if (!(toys.optflags & FLAG_PATHLEN) || i <= TT.prefix) {
					// If the old file was null, we're creating a new one.
					if (!strcmp(oldname, "/dev/null") || !oldsum) {
						printf("creating %s\n", name);
						s = strrchr(name, '/');
						if (s) {
							*s = 0;
							xmkpath(name, -1);
							*s = '/';
						}
						TT.filein = xcreate(name, O_CREAT|O_EXCL|O_RDWR, 0666);
					} else {
						printf("patching %s\n", name);
						TT.filein = xopen(name, O_RDWR);
					}
					TT.fileout = copy_tempfile(TT.filein, name, &TT.tempname);
					TT.linenum = 0;
					TT.hunknum = 0;
				}
			}

			TT.hunknum++;

			continue;
		}

		// If we didn't continue above, discard this line.
		free(patchline);
	}

	finish_oldfile();

	if (CFG_TOYBOX_FREE) {
		close(TT.filepatch);
		free(oldname);
		free(newname);
	}
}
