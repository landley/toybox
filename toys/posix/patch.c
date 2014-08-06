/* patch.c - Apply a "universal" diff.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * see http://opengroup.org/onlinepubs/9699919799/utilities/patch.html
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

USE_PATCH(NEWTOY(patch, USE_TOYBOX_DEBUG("x")"ulp#i:R", TOYFLAG_USR|TOYFLAG_BIN))

config PATCH
  bool "patch"
  default y
  help
    usage: patch [-i file] [-p depth] [-Ru]

    Apply a unified diff to one or more files.

    -i	Input file (defaults=stdin)
    -l	Loose match (ignore whitespace)
    -p	Number of '/' to strip from start of file paths (default=all)
    -R	Reverse patch.
    -u	Ignored (only handles "unified" diffs)

    This version of patch only handles unified diffs, and only modifies
    a file when all all hunks to that file apply.  Patch prints failed
    hunks to stderr, and exits with nonzero status if any hunks fail.

    A file compared against /dev/null (or with a date <= the epoch) is
    created/deleted as appropriate.
*/

#define FOR_patch
#include "toys.h"

GLOBALS(
  char *infile;
  long prefix;

  struct double_list *current_hunk;
  long oldline, oldlen, newline, newlen;
  long linenum;
  int context, state, filein, fileout, filepatch, hunknum;
  char *tempname;
)

// Dispose of a line of input, either by writing it out or discarding it.

// state < 2: just free
// state = 2: write whole line to stderr
// state = 3: write whole line to fileout
// state > 3: write line+1 to fileout when *line != state

#define PATCH_DEBUG (CFG_TOYBOX_DEBUG && (toys.optflags & 32))

static void do_line(void *data)
{
  struct double_list *dlist = (struct double_list *)data;

  if (TT.state>1 && *dlist->data != TT.state) {
    char *s = dlist->data+(TT.state>3 ? 1 : 0);
    int i = TT.state == 2 ? 2 : TT.fileout;

    xwrite(i, s, strlen(s));
    xwrite(i, "\n", 1);
  }

  if (PATCH_DEBUG) fprintf(stderr, "DO %d: %s\n", TT.state, dlist->data);

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

  fprintf(stderr, "Hunk %d FAILED %ld/%ld.\n",
      TT.hunknum, TT.oldline, TT.newline);
  toys.exitval = 1;

  // If we got to this point, we've seeked to the end.  Discard changes to
  // this file and advance to next file.

  TT.state = 2;
  llist_traverse(TT.current_hunk, do_line);
  TT.current_hunk = NULL;
  delete_tempfile(TT.filein, TT.fileout, &TT.tempname);
  TT.state = 0;
}

// Compare ignoring whitespace. Just returns
static int loosecmp(char *aa, char *bb)
{
  int a = 0, b = 0;

  for (;;) {
    while (isspace(aa[a])) a++;
    while (isspace(bb[b])) b++;
    if (aa[a] != bb[b]) return 1;
    if (!aa[a]) return 0;
    a++, b++;
  }
}

// Given a hunk of a unified diff, make the appropriate change to the file.
// This does not use the location information, but instead treats a hunk
// as a sort of regex.  Copies data from input to output until it finds
// the change to be made, then outputs the changed data and returns.
// (Finding EOF first is an error.)  This is a single pass operation, so
// multiple hunks must occur in order in the file.

static int apply_one_hunk(void)
{
  struct double_list *plist, *buf = NULL, *check;
  int matcheof = 0, reverse = toys.optflags & FLAG_R, backwarn = 0;
  int (*lcmp)(char *aa, char *bb);

  lcmp = (toys.optflags & FLAG_l) ? (void *)loosecmp : (void *)strcmp;
  dlist_terminate(TT.current_hunk);

  // Match EOF if there aren't as many ending context lines as beginning
  for (plist = TT.current_hunk; plist; plist = plist->next) {
    if (plist->data[0]==' ') matcheof++;
    else matcheof = 0;
    if (PATCH_DEBUG) fprintf(stderr, "HUNK:%s\n", plist->data);
  }
  matcheof = matcheof < TT.context;

  if (PATCH_DEBUG) fprintf(stderr,"MATCHEOF=%c\n", matcheof ? 'Y' : 'N');

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
      if (data && !lcmp(data, plist->data+1)) {
        if (!backwarn) backwarn = TT.linenum;
      }
      plist = plist->next;
    }

    // Is this EOF?
    if (!data) {
      if (PATCH_DEBUG) fprintf(stderr, "INEOF\n");

      // Does this hunk need to match EOF?
      if (!plist && matcheof) break;

      if (backwarn)
        fprintf(stderr, "Possibly reversed hunk %d at %ld\n",
            TT.hunknum, TT.linenum);

      // File ended before we found a place for this hunk.
      fail_hunk();
      goto done;
    } else if (PATCH_DEBUG) fprintf(stderr, "IN: %s\n", data);
    check = dlist_add(&buf, data);

    // Compare this line with next expected line of hunk.

    // A match can fail because the next line doesn't match, or because
    // we hit the end of a hunk that needed EOF, and this isn't EOF.

    // If match failed, flush first line of buffered data and
    // recheck buffered data for a new match until we find one or run
    // out of buffer.

    for (;;) {
      if (!plist || lcmp(check->data, plist->data+1)) {
        // Match failed.  Write out first line of buffered data and
        // recheck remaining buffered data for a new match.

        if (PATCH_DEBUG) {
          int bug = 0;

          if (!plist) fprintf(stderr, "NULL plist\n");
          else {
            while (plist->data[bug] == check->data[bug]) bug++;
            fprintf(stderr, "NOT(%d:%d!=%d): %s\n", bug, plist->data[bug],
              check->data[bug], plist->data);
          }
        }

        TT.state = 3;
        do_line(check = dlist_pop(&buf));
        plist = TT.current_hunk;

        // If we've reached the end of the buffer without confirming a
        // match, read more lines.
        if (!buf) break;
        check = buf;
      } else {
        if (PATCH_DEBUG) fprintf(stderr, "MAYBE: %s\n", plist->data);
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
  llist_traverse(TT.current_hunk, do_line);
  TT.current_hunk = NULL;
  TT.state = 1;
done:
  if (buf) {
    dlist_terminate(buf);
    llist_traverse(buf, do_line);
  }

  return TT.state;
}

// Read a patch file and find hunks, opening/creating/deleting files.
// Call apply_one_hunk() on each hunk.

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body

void patch_main(void)
{
  int reverse = toys.optflags&FLAG_R, state = 0, patchlinenum = 0,
    strip = 0;
  char *oldname = NULL, *newname = NULL;

  if (TT.infile) TT.filepatch = xopen(TT.infile, O_RDONLY);
  TT.filein = TT.fileout = -1;

  // Loop through the lines in the patch
  for (;;) {
    char *patchline;

    patchline = get_line(TT.filepatch);
    if (!patchline) break;

    // Other versions of patch accept damaged patches,
    // so we need to also.
    if (strip || !patchlinenum++) {
      int len = strlen(patchline);
      if (patchline[len-1] == '\r') {
        if (!strip) fprintf(stderr, "Removing DOS newlines\n");
        strip = 1;
        patchline[len-1]=0;
      }
    }
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

        if (!TT.oldlen && !TT.newlen) state = apply_one_hunk();
        continue;
      }
      dlist_terminate(TT.current_hunk);
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
      if (i>1900 && i<=1970) *name = xstrdup("/dev/null");
      else {
        *s = 0;
        *name = xstrdup(patchline+4);
      }

      // We defer actually opening the file because svn produces broken
      // patches that don't signal they want to create a new file the
      // way the patch man page says, so you have to read the first hunk
      // and _guess_.

    // Start a new hunk?  Usually @@ -oldline,oldlen +newline,newlen @@
    // but a missing ,value means the value is 1.
    } else if (state == 1 && !strncmp("@@ -", patchline, 4)) {
      int i;
      char *s = patchline+4;

      // Read oldline[,oldlen] +newline[,newlen]

      TT.oldlen = TT.newlen = 1;
      TT.oldline = strtol(s, &s, 10);
      if (*s == ',') TT.oldlen=strtol(s+1, &s, 10);
      TT.newline = strtol(s+2, &s, 10);
      if (*s == ',') TT.newlen = strtol(s+1, &s, 10);

      TT.context = 0;
      state = 2;

      // If this is the first hunk, open the file.
      if (TT.filein == -1) {
        int oldsum, newsum, del = 0;
        char *name;

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
        for (i = 0, s = name; *s;) {
          if ((toys.optflags & FLAG_p) && TT.prefix == i) break;
          if (*s++ != '/') continue;
          while (*s == '/') s++;
          name = s;
          i++;
        }

        if (del) {
          printf("removing %s\n", name);
          xunlink(name);
          state = 0;
        // If we've got a file to open, do so.
        } else if (!(toys.optflags & FLAG_p) || i <= TT.prefix) {
          // If the old file was null, we're creating a new one.
          if ((!strcmp(oldname, "/dev/null") || !oldsum) && access(name, F_OK))
          {
            printf("creating %s\n", name);
            if (mkpathat(AT_FDCWD, name, 0, 2))
              perror_exit("mkpath %s", name);
            TT.filein = xcreate(name, O_CREAT|O_EXCL|O_RDWR, 0666);
          } else {
            printf("patching %s\n", name);
            TT.filein = xopen(name, O_RDONLY);
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
