/* chmod.c - Change file mode bits
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/chmod.html
 *
 * Deviations from posix: -cfv

USE_CHMOD(NEWTOY(chmod, "<2?cvfR[-cvf]", TOYFLAG_BIN))

config CHMOD
  bool "chmod"
  default y
  help
    usage: chmod [-R] MODE FILE...

    Change permission mode of listed file[s] to be readable, writeable, and/or
    executable to the current user, the file's group, or other (everyone else).
    You can also set/clear the suid, guid, and sticky bits.

    -c	Print changes
    -f	Don't print errors
    -R	Recursive
    -v	Verbose

    MODE can be octal or (comma-separated) stanzas of [ugoa][+-=][rwxstXugo]

    Stanzas are applied in order: For each category (u = user,
    g = group, o = other, a = all three, if none specified default is a),
    set (+), clear (-), or copy (=), r = read, w = write, x = execute.
    s = u+s = suid, g+s = sgid, +t = sticky. (o+s ignored so a+s doesn't set +t)
    suid/sgid: execute as the user/group who owns the file.
    sticky: can't delete files you don't own out of this directory
    X = x for directories or if any category already has x set.

    Or MODE can be an octal value up to 7777	ug uuugggooo	top +
    bit 1 = o+x, bit 1<<8 = u+r, 1<<10 = g+s	sstrwxrwxrwx	bottom

    Examples:
    chmod u+w file - allow owner of "file" to write to it.
    chmod 744 file - user can read/write/execute, everyone else read only
*/

#define FOR_chmod
#include "toys.h"

GLOBALS(
  char *mode;
)

static int do_chmod(struct dirtree *try)
{
  mode_t mode, old;
  char *s, *ss;

  if (!dirtree_notdotdot(try)) return 0;

  if (FLAG(R) && try->parent && S_ISLNK(try->st.st_mode)) {
    // Ignore symlinks found during recursion. We'll only try to modify
    // symlinks mentioned directly as arguments. We'll fail, of course,
    // but that's what you asked for in that case.
  } else {
    mode = string_to_mode(TT.mode, try->st.st_mode) & ~S_IFMT;
    if (FLAG(v)||FLAG(c)) {
      mode_to_string(old = try->st.st_mode&07777, ss = toybuf+64);
      *toybuf = 0;
      if (old!=mode)
        sprintf(toybuf, "changed from %04o (%s) to", old, ss+(*ss=='-'));
      else if (FLAG(v)) strcpy(toybuf, "retained as");
      if (*toybuf) {
        s = dirtree_path(try, 0);
        mode_to_string(mode, ss);
        printf("mode of '%s' %s %04o (%s)\n", s, toybuf, mode, ss+(*ss=='-'));
        free(s);
      }
    }
    wfchmodat(dirtree_parentfd(try), try->name, mode);
  }

  return FLAG(R)*DIRTREE_RECURSE;
}

void chmod_main(void)
{
  TT.mode = *toys.optargs;
  char **file;

  // -f means discard stderr, so replace it with the read end of a pipe
  if (FLAG(f)) {
    int pp[2];

    pipe(pp);
    dup2(*pp, 2);
  }

  for (file = toys.optargs+1; *file; file++) dirtree_read(*file, do_chmod);
}
