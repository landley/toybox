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

	  A file compared against /dev/null is created/deleted as appropriate.
*/

#include "toys.h"

DEFINE_GLOBALS(
	char *infile;
	long prefix;

	struct double_list *plines, *flines;
	long oldline, oldlen, newline, newlen, linenum;
	int context, state, filein, fileout, filepatch;
	char *tempname, *oldname;
)

#define TT this.patch

#define FLAG_REVERSE 1
#define FLAG_PATHLEN 4

static void do_line(void *data)
{
	struct double_list *dlist = (struct double_list *)data;

	if (TT.state && *dlist->data != TT.state)
		fdprintf(TT.state == 2 ? 2: TT.fileout,
			"%s\n", dlist->data+(TT.state>2 ? 1 : 0));
	free(dlist->data);
}

static void finish_oldfile(void)
{
	if (TT.tempname) replace_tempfile(TT.filein, TT.fileout, &TT.tempname);
}

static void fail_hunk(void)
{
	if (!TT.plines) return;
	TT.plines->prev->next = 0;

	printf("Hunk FAILED.\n");
	toys.exitval = 1;

	// If we got to this point, we've seeked to the end.  Discard changes to
	// this file and advance to next file.

	TT.state = 2;
	llist_free(TT.plines, do_line);
	TT.plines = NULL;
	delete_tempfile(TT.filein, TT.fileout, &TT.tempname);
	TT.filein = -1;
}

static void apply_hunk(void)
{
	struct double_list *plist, *buf = NULL;
	int i = 0, backwards = 0, matcheof = 0,
		reverse = toys.optflags & FLAG_REVERSE;

	// Break doubly linked list so we can use singly linked traversal function.
	TT.plines->prev->next = NULL;

	// Match EOF if there aren't as many ending context lines as beginning
	for (plist = TT.plines; plist; plist = plist->next) {
		if (plist->data[0]==' ') i++;
		else i = 0;
	}
	if (i < TT.context) matcheof++;

	// Search for a place to apply this hunk.  Match all context lines and
	// lines to be removed.
	plist = TT.plines;
	buf = NULL;
	i = 0;

	// Start of for loop
	if (TT.context) for (;;) {
		char *data = get_line(TT.filein);
		TT.linenum++;

		// Skip lines we'd add.
		while (plist && *plist->data == "+-"[reverse]) {
			if (data && !backwards && !strcmp(data, plist->data+1)) {
				backwards = 1;
				fdprintf(2,"Possibly reversed hunk at %ld\n", TT.linenum);
			}
			plist = plist->next;
		}

		if (!data) {
			// Matched EOF?
			if (!plist && matcheof) break;
			// File ended before we found a home for this hunk?
			fail_hunk();
			goto done;
		}
		dlist_add(&buf, data);

		if (!plist || strcmp(data, plist->data+1)) {     // Ignore whitespace?
			// Match failed, hunk doesn't go here.  Flush accumulated buffer
			// so far.

			buf->prev->next = NULL;
			TT.state = 1;
			llist_free(buf, do_line);
			buf = NULL;
			plist = TT.plines;
		} else {
			// Match, advance plist.
			plist = plist->next;
			if (!plist && !matcheof) break;
		}
	}

	// Got it.  Emit changed data.
	TT.state = "-+"[reverse];
	llist_free(TT.plines, do_line);
	TT.plines = NULL;
done:
	TT.state = 0;
	if (buf) {
		buf->prev->next = NULL;
		llist_free(buf, do_line);
	}
}

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body

void patch_main(void)
{
	if (TT.infile) TT.filepatch = xopen(TT.infile, O_RDONLY);
	TT.filein = TT.fileout = -1;

	// Loop through the lines in the patch
	for(;;) {
		char *patchline;

		patchline = get_line(TT.filepatch);
		if (!patchline) break;

		// Are we processing a hunk?
		if (TT.state >= 2) {
			if (*patchline==' ' || *patchline=='+' || *patchline=='-') {
				dlist_add(&TT.plines, patchline);

				if (*patchline != '+') TT.oldlen--;
				if (*patchline != '-') TT.newlen--;

				// Context line?
				if (*patchline==' ' && TT.state==2) TT.context++;
				else TT.state=3;

				if (!TT.oldlen && !TT.newlen) apply_hunk();
				continue;
			}
			fail_hunk();
			TT.state = 0;
			continue;
		}

		// Open a new file?
		if (!strncmp("--- ", patchline, 4)) {
			char *s;
			free(TT.oldname);

			// Trim date from end of filename (if any).  We don't care.
			for (s = patchline+4; *s && *s!='\t'; s++)
				if (*s=='\\' && s[1]) s++;
			*s = 0;

			TT.oldname = xstrdup(patchline+4);
		} else if (!strncmp("+++ ", patchline, 4)) {
			int i = 0, del = 0;
			char *s, *start;

			finish_oldfile();

			// Trim date from end of filename (if any).  We don't care.
			for (s = patchline+4; *s && *s!='\t'; s++)
				if (*s=='\\' && s[1]) s++;
			*s = 0;


			// If new file is /dev/null (before -p), we're deleting oldname
			start = patchline+4;
			if (!strcmp(start, "/dev/null")) {
				start = TT.oldname;
				del++;
			} else start = patchline+4;
				
			// handle -p path truncation.
			for (s = start; *s;) {
				if ((toys.optflags & FLAG_PATHLEN) && TT.prefix == i) break;
				if (*(s++)=='/') {
					start = s;
					i++;
				}
			}

			if (del) xunlink(TT.oldname);
			// If we've got a file to open, do so.
			else if (!(toys.optflags & FLAG_PATHLEN) || i <= TT.prefix) {
				// If the old file was null, we're creating a new one.
				if (!strcmp(TT.oldname, "/dev/null")) {
					printf("creating %s\n", start);
					s = strrchr(start, '/');
					if (s) {
						*s = 0;
						xmkpath(start, -1);
						*s = '/';
					}
					TT.filein = xcreate(start, O_CREAT|O_RDWR, 0666);
				} else {
					printf("patching %s\n", start);
					TT.filein = xopen(start, O_RDWR);
				}
				TT.fileout = copy_tempfile(TT.filein, start, &TT.tempname);
				TT.state = 1;
				TT.context = 0;
				TT.linenum = 0;
			}

		// Start a new hunk?
		// Test filein rather than state to report only the first failed hunk.
		} else if (TT.filein!=-1 && !strncmp("@@ -", patchline, 4) &&
			4 == sscanf(patchline+4, "%ld,%ld +%ld,%ld", &TT.oldline,
				&TT.oldlen, &TT.newline, &TT.newlen))
		{
			TT.context = 0;
			TT.state = 2;
			continue;
		}

		// If we didn't continue above, discard this line.
		free(patchline);
	}

	finish_oldfile();

	if (CFG_TOYBOX_FREE) {
		close(TT.filepatch);
		free(TT.oldname);
	}
}
