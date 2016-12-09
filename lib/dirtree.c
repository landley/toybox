/* dirtree.c - Functions for dealing with directory trees.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 */

#include "toys.h"

int isdotdot(char *name)
{
  if (name[0]=='.' && (!name[1] || (name[1]=='.' && !name[2]))) return 1;

  return 0;
}

// Default callback, filters out "." and ".." except at top level.

int dirtree_notdotdot(struct dirtree *catch)
{
  // Should we skip "." and ".."?
  return (!catch->parent||!isdotdot(catch->name))
    *(DIRTREE_SAVE|DIRTREE_RECURSE);
}

// Create a dirtree node from a path, with stat and symlink info.
// (This doesn't open directory filehandles yet so as not to exhaust the
// filehandle space on large trees, dirtree_handle_callback() does that.)

struct dirtree *dirtree_add_node(struct dirtree *parent, char *name, int flags)
{
  struct dirtree *dt = NULL;
  struct stat st;
  int len = 0, linklen = 0;

  if (name) {
    // open code this because haven't got node to call dirtree_parentfd() on yet
    int fd = parent ? parent->dirfd : AT_FDCWD;

    if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW*!(flags&DIRTREE_SYMFOLLOW)))
      goto error;
    if (S_ISLNK(st.st_mode)) {
      if (0>(linklen = readlinkat(fd, name, libbuf, 4095))) goto error;
      libbuf[linklen++]=0;
    }
    len = strlen(name);
  }
  dt = xzalloc((len = sizeof(struct dirtree)+len+1)+linklen);
  dt->parent = parent;
  if (name) {
    memcpy(&(dt->st), &st, sizeof(struct stat));
    strcpy(dt->name, name);

    if (linklen) dt->symlink = memcpy(len+(char *)dt, libbuf, linklen);
  }

  return dt;

error:
  if (!(flags&DIRTREE_SHUTUP) && !isdotdot(name)) {
    char *path = parent ? dirtree_path(parent, 0) : "";

    perror_msg("%s%s%s", path, parent ? "/" : "", name);
    if (parent) free(path);
  }
  if (parent) parent->symlink = (char *)1;
  free(dt);
  return 0;
}

// Return path to this node, assembled recursively.

// Initial call can pass in NULL to plen, or point to an int initialized to 0
// to return the length of the path, or a value greater than 0 to allocate
// extra space if you want to append your own text to the string.

char *dirtree_path(struct dirtree *node, int *plen)
{
  char *path;
  int len;

  if (!node) {
    path = xmalloc(*plen);
    *plen = 0;
    return path;
  }

  len = (plen ? *plen : 0)+strlen(node->name)+1;
  path = dirtree_path(node->parent, &len);
  if (len && path[len-1] != '/') path[len++]='/';
  len = (stpcpy(path+len, node->name) - path);
  if (plen) *plen = len;

  return path;
}

int dirtree_parentfd(struct dirtree *node)
{
  return node->parent ? node->parent->dirfd : AT_FDCWD;
}

// Handle callback for a node in the tree. Returns saved node(s) if
// callback returns DIRTREE_SAVE, otherwise frees consumed nodes and
// returns NULL. If !callback return top node unchanged.
// If !new return DIRTREE_ABORTVAL

struct dirtree *dirtree_handle_callback(struct dirtree *new,
          int (*callback)(struct dirtree *node))
{
  int flags;

  if (!new) return DIRTREE_ABORTVAL;
  if (!callback) return new;
  flags = callback(new);

  if (S_ISDIR(new->st.st_mode) && (flags & (DIRTREE_RECURSE|DIRTREE_COMEAGAIN)))
    flags = dirtree_recurse(new, callback,
      openat(dirtree_parentfd(new), new->name, O_CLOEXEC), flags);

  // If this had children, it was callback's job to free them already.
  if (!(flags & DIRTREE_SAVE)) {
    free(new);
    new = NULL;
  }

  return (flags & DIRTREE_ABORT)==DIRTREE_ABORT ? DIRTREE_ABORTVAL : new;
}

// Recursively read/process children of directory node, filtering through
// callback(). Uses and closes supplied ->dirfd.

int dirtree_recurse(struct dirtree *node,
          int (*callback)(struct dirtree *node), int dirfd, int flags)
{
  struct dirtree *new, **ddt = &(node->child);
  struct dirent *entry;
  DIR *dir;

  node->dirfd = dirfd;
  if (node->dirfd == -1 || !(dir = fdopendir(node->dirfd))) {
    if (!(flags & DIRTREE_SHUTUP)) {
      char *path = dirtree_path(node, 0);
      perror_msg("No %s", path);
      free(path);
    }
    close(node->dirfd);

    return flags;
  }

  // according to the fddir() man page, the filehandle in the DIR * can still
  // be externally used by things that don't lseek() it.

  // The extra parentheses are to shut the stupid compiler up.
  while ((entry = readdir(dir))) {
    if ((flags&DIRTREE_PROC) && !isdigit(*entry->d_name)) continue;
    if (!(new = dirtree_add_node(node, entry->d_name, flags))) continue;
    new = dirtree_handle_callback(new, callback);
    if (new == DIRTREE_ABORTVAL) break;
    if (new) {
      *ddt = new;
      ddt = &((*ddt)->next);
    }
  }

  if (flags & DIRTREE_COMEAGAIN) {
    node->again++;
    flags = callback(node);
  }

  // This closes filehandle as well, so note it
  closedir(dir);
  node->dirfd = -1;

  return flags;
}

// Create dirtree from path, using callback to filter nodes. If !callback
// return just the top node. Use dirtree_notdotdot callback to allocate a
// tree of struct dirtree nodes and return pointer to root node for later
// processing.
// Returns DIRTREE_ABORTVAL if path didn't exist (use DIRTREE_SHUTUP to handle
// error message yourself).

struct dirtree *dirtree_flagread(char *path, int flags,
  int (*callback)(struct dirtree *node))
{
  return dirtree_handle_callback(dirtree_add_node(0, path, flags), callback);
}

// Common case
struct dirtree *dirtree_read(char *path, int (*callback)(struct dirtree *node))
{
  return dirtree_flagread(path, 0, callback);
}
