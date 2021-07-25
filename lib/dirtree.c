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
  struct dirtree *dt = 0;
  struct stat st;
  int len = 0, linklen = 0, statless = 0;

  if (name) {
    // open code fd = because haven't got node to call dirtree_parentfd() on yet
    int fd = parent ? parent->dirfd : AT_FDCWD,
      sym = AT_SYMLINK_NOFOLLOW*!(flags&DIRTREE_SYMFOLLOW);

    // stat dangling symlinks
    if (fstatat(fd, name, &st, sym)) {
      // If we got ENOENT without NOFOLLOW, try again with NOFOLLOW.
      if (errno!=ENOENT || sym || fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW)) {
        if (flags&DIRTREE_STATLESS) statless++;
        else goto error;
      }
    }
    if (!statless && S_ISLNK(st.st_mode)) {
      if (0>(linklen = readlinkat(fd, name, libbuf, 4095))) goto error;
      libbuf[linklen++]=0;
    }
    len = strlen(name);
  }

  // Allocate/populate return structure
  memset(dt = xmalloc((len = sizeof(struct dirtree)+len+1)+linklen), 0,
    statless ? sizeof(struct dirtree) : offsetof(struct dirtree, st));
  dt->parent = parent;
  dt->again = statless ? 2 : 0;
  if (!statless) memcpy(&dt->st, &st, sizeof(struct stat));
  if (name) strcpy(dt->name, name);
  else *dt->name = 0, dt->st.st_mode = S_IFDIR;
  if (linklen) dt->symlink = memcpy(len+(char *)dt, libbuf, linklen);

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

// Return path to this node.

// Initial call can pass in NULL to plen, or point to an int initialized to 0
// to return the length of the path, or a value greater than 0 to allocate
// extra space if you want to append your own text to the string.

char *dirtree_path(struct dirtree *node, int *plen)
{
  struct dirtree *nn;
  char *path;
  int ii, ll, len;

  ll = len = plen ? *plen : 0;
  if (!node->parent)
    return strcpy(xmalloc(strlen(node->name)+ll+1), node->name);
  for (nn = node; nn; nn = nn->parent)
    if ((ii = strlen(nn->name))) len += ii+1-(nn->name[ii-1]=='/');
  if (plen) *plen = len;
  path = xmalloc(len)+len-ll;
  for (nn = node; nn; nn = nn->parent) if ((len = strlen(nn->name))) {
    *--path = '/'*(nn != node);
    if (nn->name[len-1]=='/') len--;
    memcpy(path -= len, nn->name, len);
  }

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

static struct dirtree *dirtree_handle_callback(struct dirtree *new,
  int (*callback)(struct dirtree *node))
{
  int flags;

  if (!new) return DIRTREE_ABORTVAL;
  if (!callback) return new;
  flags = callback(new);

  if (S_ISDIR(new->st.st_mode) && (flags & (DIRTREE_RECURSE|DIRTREE_COMEAGAIN)))
    flags = dirtree_recurse(new, callback, !*new->name ? AT_FDCWD :
      openat(dirtree_parentfd(new), new->name, O_CLOEXEC), flags);

  // Free node that didn't request saving and has no saved children.
  if (!new->child && !(flags & DIRTREE_SAVE)) {
    free(new);
    new = 0;
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
  DIR *dir = 0;

  // Why doesn't fdopendir() support AT_FDCWD?
  if (AT_FDCWD == (node->dirfd = dirfd)) dir = opendir(".");
  else if (node->dirfd != -1) dir = fdopendir(node->dirfd);
  if (!dir) {
    if (!(flags & DIRTREE_SHUTUP)) {
      char *path = dirtree_path(node, 0);
      perror_msg_raw(path);
      free(path);
    }
    close(node->dirfd);

    return flags;
  }

  // according to the fddir() man page, the filehandle in the DIR * can still
  // be externally used by things that don't lseek() it.

  while ((entry = readdir(dir))) {
    if ((flags&DIRTREE_PROC) && !isdigit(*entry->d_name)) continue;
    if (!(new = dirtree_add_node(node, entry->d_name, flags))) continue;
    if (!new->st.st_blksize && !new->st.st_mode)
      new->st.st_mode = entry->d_type<<12;
    new = dirtree_handle_callback(new, callback);
    if (new == DIRTREE_ABORTVAL) break;
    if (new) {
      *ddt = new;
      ddt = &((*ddt)->next);
    }
  }

  if (flags & DIRTREE_COMEAGAIN) {
    node->again |= 1;
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
