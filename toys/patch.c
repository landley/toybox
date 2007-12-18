/* vi: set sw=4 ts=4: */
/*
 * patch.c - Apply a "universal" diff.
 *
 * SUSv3 at http://www.opengroup.org/onlinepubs/009695399/utilities/patch.html
 * but who cares about "ed"?
 *
 * -u ignored
 * -R reverse (remove applied hunks, apply removed hunks)
 * -p num remove this many slashes from start of path (default = all)
 *
 * TODO:
 * -b backup
 * -l treat all whitespace as a single space
 * -N ignore already applied
 * -d chdir first
 * -D define wrap #ifdef and #ifndef around changes
 * -i patchfile apply patch from filename rather than stdin
 * -o outfile output here instead of in place
 * -r rejectfile write rejected hunks to this file
 *
 * -E remove empty files --remove-empty-files
 * -f force (no questions asked)
 * -F fuzz (number, default 2)
 * [file] which file to patch
 */

#include "toys.h"

#define TT toy.patch

#define FLAG_REVERSE 1
#define FLAG_PATHLEN 4

static void do_line(void *data)
{
	struct double_list *dlist = (struct double_list *)data;

	if (TT.state && *dlist->data != TT.state)
		fdprintf(TT.fileout, "%s\n", dlist->data+(TT.state>1 ? 1 : 0));
	free(dlist->data);
	free(dlist);
}


static void dlist_add(struct double_list **list, char *data)
{
	struct double_list *line = xmalloc(sizeof(struct double_list));

	line->data = data;
	if (*list) {
		line->next = *list;
		line->prev = (*list)->prev;
		(*list)->prev->next = line;
		(*list)->prev = line;
	} else *list = line->next = line->prev = line;
}

static void apply_hunk(void)
{
	struct double_list *plist, *temp, *buf;
	int i = 0, backwards = 0, reverse = toys.optflags & FLAG_REVERSE;

	TT.state = 0;

	if (!TT.plines) return;
	temp = buf = NULL;

	// Hunk is complete, break doubly linked list so we can use singly linked
	// traversal function.
	TT.plines->prev->next = NULL;

	// Trim extra context lines, if any.  If there aren't as many ending
	// context lines as beginning lines, this isn't a valid hunk.
	for (plist = TT.plines; plist; plist = plist->next) {
		if (plist->data[0]==' ') {
			if (i<TT.context) temp = plist;
			i++;
		} else i = 0;
	}
	if (i < TT.context) goto fail_hunk;
	llist_free(temp->next, do_line);
	temp->next = NULL;

	// Search for a place to apply this hunk
	plist = TT.plines;
	buf = NULL;
	i = 0;
	for (;;) {
		char *data = get_line(TT.filein);
		TT.linenum++;

		// If the file ended before we found a home for this hunk, fail.
		if (!data) break;

		dlist_add(&buf, data);
		if (!backwards && *plist->data == "+-"[reverse]) {
			backwards = 1;
			if (!strcmp(data, plist->data+1))
				fdprintf(1,"Possibly reversed hunk at %ld\n", TT.linenum);
		}
		while (*plist->data == "+-"[reverse]) plist = plist->next;
		if (strcmp(data, plist->data+1)) {     // Ignore whitespace?
			// Hunk doesn't go here, flush accumulated buffer so far.

			buf->prev->next = NULL;
			TT.state = 1;
			llist_free(buf, do_line);
			buf = NULL;
			plist = TT.plines;
		} else {
			plist = plist->next;
			if (!plist) {
				// Got it.  Emit changed data.
				TT.state = "-+"[reverse];
				llist_free(TT.plines, do_line);
				TT.plines = NULL;
				buf->prev->next = NULL;
				TT.state = 0;
				llist_free(buf, do_line);
				return;
			}
		}
	}
fail_hunk:
	printf("Hunk FAILED.\n");

	// If we got to this point, we've seeked to the end.  Discard changes to
	// this file and advance to next file.

	TT.state = 0;
	llist_free(TT.plines, do_line);
	TT.plines = 0;
	if (buf) {
		buf->prev->next = NULL;
		llist_free(buf, do_line);
	}
	delete_tempfile(TT.filein, TT.fileout, &TT.tempfile);
	TT.filein = -1;
}

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body

void patch_main(void)
{
	if (TT.infile) TT.filepatch = xopen(TT.infile, O_RDONLY);
	else TT.filepatch = 0;
	TT.filein = TT.fileout = -1;

	// Loop through the lines in the patch
	for(;;) {
		char *patchline;

		patchline = get_line(TT.filepatch);
		if (!patchline) break;

		// Are we processing a hunk?
		if (TT.state >= 2) {
			// Context line?
			if (*patchline==' ' || *patchline=='+' || *patchline=='-') {
				dlist_add(&TT.plines, patchline);

				if (*patchline==' ' && TT.state==2) TT.context++;
				else TT.state=3;

				continue;
			}
		}

		// If we have a hunk at this point, it's ready to apply.
		apply_hunk();
			
		// Open a new file?
		if (!strncmp("+++ ", patchline, 4)) {
			int i;
			char *s, *start;

			// Finish old file.
			if (TT.tempfile)
				replace_tempfile(TT.filein, TT.fileout, &TT.tempfile);

			// Trim date from end of filename (if any).  We don't care.
			for (s = patchline+4; *s && *s!='\t'; s++)
				if (*s=='\\' && s[1]) s++;
			*s = i = 0;
			for (s = start = patchline+4; *s;) {
				if ((toys.optflags & FLAG_PATHLEN) && TT.prefix == i) break;
				if (*(s++)=='/') {
					start = s;
					i++;
				}
			}

			// If we've got a file to open, do so.
			if (!(toys.optflags & FLAG_PATHLEN) || i <= TT.prefix) {
				printf("patching %s\n", start);
				TT.filein = xopen(start, O_RDWR);
				TT.fileout = copy_tempfile(TT.filein, start, &TT.tempfile);
				TT.state = 1;
				TT.context = 0;
				TT.linenum = 0;
			}

		// Start a new hunk?
		} else if (TT.filein!=-1 && !strncmp("@@ ", patchline, 3)) {
			TT.context = 0;
			TT.state = 2;
			sscanf(patchline+3, "%ld,%ld %ld,%ld", &TT.oldline,
				&TT.oldlen, &TT.newline, &TT.newlen);
			continue;
		}

		// This line is noise, discard it.
		free(patchline);
	}

	// Flush pending hunk and flush data
	apply_hunk();
	if (TT.tempfile) replace_tempfile(TT.filein, TT.fileout, &TT.tempfile);
	close(TT.filepatch);
}
