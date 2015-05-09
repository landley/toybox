/* dirtree.c - Functions for dealing with directory trees.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 */

#include "toys.h"

static int notdotdot(char *name)
{
  if (name[0]=='.' && (!name[1] || (name[1]=='.' && !name[2]))) return 0;

  return 1;
}

// Default callback, filters out "." and "..".

int dirtree_notdotdot(struct dirtree *catch)
{
  // Should we skip "." and ".."?
  return notdotdot(catch->name) ? DIRTREE_SAVE|DIRTREE_RECURSE : 0;
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
    int fd = parent ? parent->data : AT_FDCWD;

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

    if (linklen) {
      dt->symlink = memcpy(len+(char *)dt, libbuf, linklen);
      dt->data = --linklen;
    }
  }

  return dt;

error:
  if (!(flags&DIRTREE_SHUTUP) && notdotdot(name)) {
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
  return node->parent ? node->parent->data : AT_FDCWD;
}

// Handle callback for a node in the tree. Returns saved node(s) or NULL.
//
// By default, allocates a tree of struct dirtree, not following symlinks
// If callback==NULL, or callback always returns 0, allocate tree of struct
// dirtree and return root of tree.  Otherwise call callback(node) on each
// hit, free structures after use, and return NULL.
//

struct dirtree *dirtree_handle_callback(struct dirtree *new,
          int (*callback)(struct dirtree *node))
{
  int flags;

  if (!new) return 0;
  if (!callback) callback = dirtree_notdotdot;
  flags = callback(new);

  if (S_ISDIR(new->st.st_mode)) {
    if (flags & (DIRTREE_RECURSE|DIRTREE_COMEAGAIN)) {
      new->data = openat(dirtree_parentfd(new), new->name, O_CLOEXEC);
      flags = dirtree_recurse(new, callback, flags);
    }
  }

  // If this had children, it was callback's job to free them already.
  if (!(flags & DIRTREE_SAVE)) {
    free(new);
    new = NULL;
  }

  return (flags & DIRTREE_ABORT)==DIRTREE_ABORT ? DIRTREE_ABORTVAL : new;
}

// Recursively read/process children of directory node (with dirfd in data),
// filtering through callback().

int dirtree_recurse(struct dirtree *node,
          int (*callback)(struct dirtree *node), int flags)
{
  struct dirtree *new, **ddt = &(node->child);
  struct dirent *entry;
  DIR *dir;

  if (node->data == -1 || !(dir = fdopendir(node->data))) {
    if (!(flags & DIRTREE_SHUTUP)) {
      char *path = dirtree_path(node, 0);
      perror_msg("No %s", path);
      free(path);
    }
    close(node->data);

    return flags;
  }

  // according to the fddir() man page, the filehandle in the DIR * can still
  // be externally used by things that don't lseek() it.

  // The extra parentheses are to shut the stupid compiler up.
  while ((entry = readdir(dir))) {
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
  node->data = -1;

  return flags;
}

// Create dirtree root
struct dirtree *dirtree_start(char *name, int symfollow)
{
  return dirtree_add_node(0, name, DIRTREE_SYMFOLLOW*!!symfollow);
}

// Create dirtree from path, using callback to filter nodes.
// If callback == NULL allocate a tree of struct dirtree nodes and return
// pointer to root node.

struct dirtree *dirtree_read(char *path, int (*callback)(struct dirtree *node))
{
  struct dirtree *root = dirtree_start(path, 0);

  return root ? dirtree_handle_callback(root, callback) : DIRTREE_ABORTVAL;
}
