/* getmountlist.c - Get a linked list of mount points, with stat information.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"
#include <mntent.h>

// Traverse arg_list of csv, calling callback on each value
void comma_args(struct arg_list *al, void *data, char *err,
  char *(*callback)(void *data, char *str, int len))
{
  char *next, *arg;
  int len;

  if (CFG_TOYBOX_DEBUG && !err) err = "INTERNAL";

  while (al) {
    arg = al->arg;
    while ((next = comma_iterate(&arg, &len)))
      if ((next = callback(data, next, len)))
        error_exit("%s '%s'\n%*c", err, al->arg,
          (int)(5+strlen(toys.which->name)+strlen(err)+next-al->arg), '^');
    al = al->next;
  }
}

// Realloc *old with oldstring,newstring

void comma_collate(char **old, char *new)
{
  char *temp, *atold = *old;

  // Only add a comma if old string didn't end with one
  if (atold && *atold) {
    char *comma = ",";

    if (atold[strlen(atold)-1] == ',') comma = "";
    temp = xmprintf("%s%s%s", atold, comma, new);
  } else temp = xstrdup(new);
  free (atold);
  *old = temp;
}

// iterate through strings in a comma separated list.
// returns start of next entry or NULL if none
// sets *len to length of entry (not including comma)
// advances *list to start of next entry
char *comma_iterate(char **list, int *len)
{
  char *start = *list, *end;

  if (!*list || !**list) return 0;

  if (!(end = strchr(*list, ','))) {
    *len = strlen(*list);
    *list = 0;
  } else *list += (*len = end-start)+1;

  return start;
}

static void octal_deslash(char *s)
{
  char *o = s;

  while (*s) {
    if (*s == '\\') {
      int i, oct = 0;

      for (i = 1; i < 4; i++) {
        if (!isdigit(s[i])) break;
        oct = (oct<<3)+s[i]-'0';
      }
      if (i == 4) {
        *o++ = oct;
        s += i;
        continue;
      }
    }
    *o++ = *s++;
  }

  *o = 0;
}

// check all instances of opt and "no"opt in optlist, return true if opt
// found and last instance wasn't no. If clean, remove each instance from list.
int comma_scan(char *optlist, char *opt, int clean)
{
  int optlen = strlen(opt), len, no, got = 0;

  if (optlist) for (;;) {
    char *s = comma_iterate(&optlist, &len);

    if (!s) break;
    no = 2*(*s == 'n' && s[1] == 'o');
    if (optlen == len-no && !strncmp(opt, s+no, optlen)) {
      got = !no;
      if (clean) {
        if (optlist) memmove(s, optlist, strlen(optlist)+1);
        else *s = 0;
      }
    }
  }

  return got;
}

// return true if all scanlist options enabled in optlist
int comma_scanall(char *optlist, char *scanlist)
{
  int i = 1;

  while (scanlist && *scanlist) {
    char *opt = comma_iterate(&scanlist, &i), *s = xstrndup(opt, i);

    i = comma_scan(optlist, s, 0);
    free(s);
    if (!i) break;
  }

  return i;
}

// Check if this type matches list.
// Odd syntax: typelist all yes = if any, typelist all no = if none.

int mountlist_istype(struct mtab_list *ml, char *typelist)
{
  int len, skip;
  char *t;

  if (!typelist) return 1;

  skip = strncmp(typelist, "no", 2);

  for (;;) {
    if (!(t = comma_iterate(&typelist, &len))) break;
    if (!skip) {
      // If one -t starts with "no", the rest must too
      if (strncmp(t, "no", 2)) error_exit("bad typelist");
      if (!strncmp(t+2, ml->type, len-2)) {
        skip = 1;
        break;
      }
    } else if (!strncmp(t, ml->type, len) && !ml->type[len]) {
      skip = 0;
      break;
    }
  }

  return !skip;
}

// Get list of mounted filesystems, including stat and statvfs info.
// Returns a reversed list, which is good for finding overmounts and such.

struct mtab_list *xgetmountlist(char *path)
{
  struct mtab_list *mtlist = 0, *mt;
  struct mntent *me;
  FILE *fp;
  char *p = path ? path : "/proc/mounts";

  if (!(fp = setmntent(p, "r"))) perror_exit("bad %s", p);

  // The "test" part of the loop is done before the first time through and
  // again after each "increment", so putting the actual load there avoids
  // duplicating it. If the load was NULL, the loop stops.

  while ((me = getmntent(fp))) {
    mt = xzalloc(sizeof(struct mtab_list) + strlen(me->mnt_fsname) +
      strlen(me->mnt_dir) + strlen(me->mnt_type) + strlen(me->mnt_opts) + 4);
    dlist_add_nomalloc((void *)&mtlist, (void *)mt);

    // Collect details about mounted filesystem
    // Don't report errors, just leave data zeroed
    if (!path) {
      stat(me->mnt_dir, &(mt->stat));
      statvfs(me->mnt_dir, &(mt->statvfs));
    }

    // Remember information from /proc/mounts
    mt->dir = stpcpy(mt->type, me->mnt_type)+1;
    mt->device = stpcpy(mt->dir, me->mnt_dir)+1;
    mt->opts = stpcpy(mt->device, me->mnt_fsname)+1;
    strcpy(mt->opts, me->mnt_opts);

    octal_deslash(mt->dir);
    octal_deslash(mt->device);
  }
  endmntent(fp);

  return mtlist;
}
