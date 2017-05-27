/* du.c - disk usage program.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/du.html
 *
 * TODO: cleanup

USE_DU(NEWTOY(du, "d#<0hmlcaHkKLsx[-HL][-kKmh]", TOYFLAG_USR|TOYFLAG_BIN))

config DU
  bool "du"
  default y
  help
    usage: du [-d N] [-askxHLlmc] [file...]

    Show disk usage, space consumed by files and directories.

    Size in:
    -k	1024 byte blocks (default)
    -K	512 byte blocks (posix)
    -m	megabytes
    -h	human readable format (e.g., 1K 243M 2G )

    What to show:
    -a	all files, not just directories
    -H	follow symlinks on cmdline
    -L	follow all symlinks
    -s	only total size of each argument
    -x	don't leave this filesystem
    -c	cumulative total
    -d N	only depth < N
    -l	disable hardlink filter
*/

#define FOR_du
#include "toys.h"

GLOBALS(
  long maxdepth;

  unsigned long depth, total;
  dev_t st_dev;
  void *inodes;
)

typedef struct node_size {
  struct dirtree *node;
  long size;
} node_size;

// Print the size and name, given size in bytes
static void print(long long size, struct dirtree *node)
{
  char *name = "total";

  if (TT.maxdepth && TT.depth > TT.maxdepth) return;

  if (toys.optflags & FLAG_h) {
    human_readable(toybuf, size, 0);
    printf("%s", toybuf);
  } else {
    int bits = 10;

    if (toys.optflags & FLAG_K) bits = 9;
    else if (toys.optflags & FLAG_m) bits = 20;

    printf("%llu", (size>>bits)+!!(size&((1<<bits)-1)));
  }
  if (node) name = dirtree_path(node, NULL);
  xprintf("\t%s\n", name);
  if (node) free(name);
}

// Return whether or not we've seen this inode+dev, adding it to the list if
// we haven't.
static int seen_inode(void **list, struct stat *st)
{
  if (!st) llist_traverse(st, free);

  // Skipping dir nodes isn't _quite_ right. They're not hardlinked, but could
  // be bind mounted. Still, it's more efficient and the archivers can't use
  // hardlinked directory info anyway. (Note that we don't catch bind mounted
  // _files_ because it doesn't change st_nlink.)
  else if (!S_ISDIR(st->st_mode) && st->st_nlink > 1) {
    struct inode_list {
      struct inode_list *next;
      ino_t ino;
      dev_t dev;
    } *new;

    for (new = *list; new; new = new->next)
      if(new->ino == st->st_ino && new->dev == st->st_dev)
        return 1;

    new = xzalloc(sizeof(*new));
    new->ino = st->st_ino;
    new->dev = st->st_dev;
    new->next = *list;
    *list = new;
  }

  return 0;
}

// dirtree callback, compute/display size of node
static int do_du(struct dirtree *node)
{
  unsigned long blocks;

  if (!node->parent) TT.st_dev = node->st.st_dev;
  else if (!dirtree_notdotdot(node)) return 0;

  // detect swiching filesystems
  if ((toys.optflags & FLAG_x) && (TT.st_dev != node->st.st_dev))
    return 0;

  // Don't loop endlessly on recursive directory symlink
  if (toys.optflags & FLAG_L) {
    struct dirtree *try = node;

    while ((try = try->parent))
      if (node->st.st_dev==try->st.st_dev && node->st.st_ino==try->st.st_ino)
        return 0;
  }

  // Don't count hard links twice
  if (!(toys.optflags & FLAG_l) && !node->again)
    if (seen_inode(&TT.inodes, &node->st)) return 0;

  // Collect child info before printing directory size
  if (S_ISDIR(node->st.st_mode)) {
    if (!node->again) {
      TT.depth++;
      return DIRTREE_COMEAGAIN|(DIRTREE_SYMFOLLOW*!!(toys.optflags&FLAG_L));
    } else TT.depth--;
  }

  // Modern compilers' optimizers are insane and think signed overflow
  // behaves differently than unsigned overflow. Sigh. Big hammer.
  blocks = node->st.st_blocks + (unsigned long)node->extra;
  node->extra = blocks;
  if (node->parent)
    node->parent->extra = (unsigned long)node->parent->extra+blocks;
  else TT.total += node->extra;

  if ((toys.optflags & FLAG_a) || !node->parent
      || (S_ISDIR(node->st.st_mode) && !(toys.optflags & FLAG_s)))
  {
    blocks = node->extra;
    print(blocks*512LL, node);
  }

  return 0;
}

void du_main(void)
{
  char *noargs[] = {".", 0}, **args;

  // Loop over command line arguments, recursing through children
  for (args = toys.optc ? toys.optargs : noargs; *args; args++)
    dirtree_flagread(*args, DIRTREE_SYMFOLLOW*!!(toys.optflags&(FLAG_H|FLAG_L)),
      do_du);
  if (toys.optflags & FLAG_c) print(TT.total*512, 0);

  if (CFG_TOYBOX_FREE) seen_inode(TT.inodes, 0);
}
