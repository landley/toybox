/* vi: set sw=4 ts=4 :*/
/* dirtree.c - Functions for dealing with directory trees.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// NOTE: This uses toybuf.  Possibly it shouldn't do that.

// Create a dirtree node from a path.

struct dirtree *dirtree_add_node(char *path)
{
	struct dirtree *dt;
	char *name;

	// Find last chunk of name.

	for (;;) {
		name = strrchr(path, '/');

		if (!name) name = path;
		else {
			if (*(name+1)) name++;
			else {
				*name=0;
				continue;
			}
		}
		break;
	}

   	dt = xzalloc(sizeof(struct dirtree)+strlen(name)+1);
	if (lstat(path, &(dt->st))) {
		error_msg("Skipped '%s'",name);
		free(dt);
		return 0;
	}
	strcpy(dt->name, name);

	return dt;
}

// Given a directory (in a writeable PATH_MAX buffer), recursively read in a
// directory tree.
//
// If callback==NULL, allocate tree of struct dirtree and
// return root of tree.  Otherwise call callback(node) on each hit, free
// structures after use, and return NULL.

struct dirtree *dirtree_read(char *path, struct dirtree *parent,
					int (*callback)(char *path, struct dirtree *node))
{
	struct dirtree *dtroot = NULL, *this, **ddt = &dtroot;
	DIR *dir;
	int len = strlen(path);

	if (!(dir = opendir(path))) perror_msg("No %s", path);
	else for (;;) {
		int norecurse = 0;
		struct dirent *entry = readdir(dir);
		if (!entry) {
			closedir(dir);
			break;
		}

		// Skip "." and ".."
		if (entry->d_name[0]=='.') {
			if (!entry->d_name[1]) continue;
			if (entry->d_name[1]=='.' && !entry->d_name[2]) continue;
		}

		snprintf(path+len, sizeof(toybuf)-len, "/%s", entry->d_name);
		*ddt = this = dirtree_add_node(path);
		if (!this) continue;
		this->parent = parent;
		this->depth = parent ? parent->depth + 1 : 1;
		if (callback) norecurse = callback(path, this);
		if (!norecurse && S_ISDIR(this->st.st_mode))
			this->child = dirtree_read(path, this, callback);
		if (callback) free(this);
		else ddt = &(this->next);
		path[len]=0;
	}

	return dtroot;
}


