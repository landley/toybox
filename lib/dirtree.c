/* vi: set sw=4 ts=4 :*/
/* dirtree.c - Functions for dealing with directory trees.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Create a dirtree node from a path, with stat and symlink info.
// (This doesn't open directory filehandles yet so as not to exhaust the
// filehandle space on large trees. handle_callback() does that instead.)

struct dirtree *dirtree_add_node(int dirfd, char *name, int symfollow)
{
	struct dirtree *dt = NULL;
	struct stat st;
	char buf[4096];
	int len = 0, linklen = 0;

	if (name) {
		if (fstatat(dirfd, name, &st, symfollow ? 0 : AT_SYMLINK_NOFOLLOW))
			goto error;
		if (S_ISLNK(st.st_mode)) {
			if (0>(linklen = readlinkat(dirfd, name, buf, 4095))) goto error;
			buf[linklen++]=0;
		}
		len = strlen(name);
	}
   	dt = xzalloc((len = sizeof(struct dirtree)+len+1)+linklen);
	if (name) {
		memcpy(&(dt->st), &st, sizeof(struct stat));
		strcpy(dt->name, name);

		if (linklen) {
			dt->symlink = memcpy(len+(char *)dt, buf, linklen);
			dt->data = --linklen;
		}
	}

	return dt;

error:
	perror_msg("%s",name);
	free(dt);
	return 0;
}

// Return path to this node, assembled recursively.

char *dirtree_path(struct dirtree *node, int *plen)
{
	char *path;
	int len;

	if (!node || !node->name) {
		path = xmalloc(*plen);
		*plen = 0;
		return path;
	}

	len = (plen ? *plen : 0)+strlen(node->name)+1;
	path = dirtree_path(node->parent, &len);
	if (len) path[len++]='/';
	len = (stpcpy(path+len, node->name) - path);
	if (plen) *plen = len;

	return path;
}

// Default callback, filters out "." and "..".

int dirtree_notdotdot(struct dirtree *catch)
{
	// Should we skip "." and ".."?
	if (catch->name[0]=='.' && (!catch->name[1] ||
			(catch->name[1]=='.' && !catch->name[2])))
				return 0;

	return DIRTREE_SAVE|DIRTREE_RECURSE;
}

// get open filehandle for node in extra, giving caller the option of
// using DIRTREE_COMEAGAIN or not.
int dirtree_opennode(struct dirtree *try)
{
	if (!dirtree_notdotdot(try)) return 0;
	if (S_ISDIR(try->st.st_mode)) {
		if (!try->extra) {
			try->extra = xdup(try->data);
			return DIRTREE_COMEAGAIN;
		}
	} else try->extra = openat(try->parent ? try->parent->data : AT_FDCWD,
		try->name, 0);

	return DIRTREE_SAVE|DIRTREE_RECURSE;
}

// Handle callback for a node in the tree. Returns saved node(s) or NULL.
//
// By default, allocates a tree of struct dirtree, not following symlinks
// If callback==NULL, or callback always returns 0, allocate tree of struct
// dirtree and return root of tree.  Otherwise call callback(node) on each
// hit, free structures after use, and return NULL.
//

struct dirtree *handle_callback(struct dirtree *new,
					int (*callback)(struct dirtree *node))
{
	int flags, dir = S_ISDIR(new->st.st_mode);

	if (!callback) callback = dirtree_notdotdot;

	// Directory always has filehandle for examining contents. Whether or
	// not we'll recurse into it gets decided later.

	if (dir)
		new->data = openat(new->parent ? new->parent->data : AT_FDCWD,
			new->name, 0);

	flags = callback(new);

	if (dir) {
		if (flags & (DIRTREE_RECURSE|DIRTREE_COMEAGAIN)) {
			dirtree_recurse(new, callback, flags & DIRTREE_SYMFOLLOW);
			if (flags & DIRTREE_COMEAGAIN) flags = callback(new);
		} else close(new->data);
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

void dirtree_recurse(struct dirtree *node,
					int (*callback)(struct dirtree *node), int symfollow)
{
	struct dirtree *new, **ddt = &(node->child);
	struct dirent *entry;
	DIR *dir;

	if (!(dir = fdopendir(node->data))) {
		char *path = dirtree_path(node, 0);
		perror_msg("No %s", path);
		free(path);
		close(node->data);

		return;
	}

	// according to the fddir() man page, the filehandle in the DIR * can still
	// be externally used by things that don't lseek() it.

	// The extra parentheses are to shut the stupid compiler up.
	while ((entry = readdir(dir))) {
		if (!(new = dirtree_add_node(node->data, entry->d_name, symfollow)))
			continue;
		new->parent = node;
		new = handle_callback(new, callback);
		if (new == DIRTREE_ABORTVAL) break;
		if (new) {
			*ddt = new;
			ddt = &((*ddt)->next);
		}
	}

	// This closes filehandle as well, so note it
	closedir(dir);
	node->data = -1;
}

// Create dirtree from path, using callback to filter nodes.
// If callback == NULL allocate a tree of struct dirtree nodes and return
// pointer to root node.

struct dirtree *dirtree_read(char *path, int (*callback)(struct dirtree *node))
{
	struct dirtree *root = dirtree_add_node(AT_FDCWD, path, 0);

	return root ? handle_callback(root, callback) : DIRTREE_ABORTVAL;
}
