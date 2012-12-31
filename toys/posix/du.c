/* du.c - disk usage program.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/du.html

USE_DU(NEWTOY(du, "d#<0hmlcaHkLsx", TOYFLAG_USR|TOYFLAG_BIN))

config DU
  bool "du"
  default y
  help
    usage: du [-d N] [-askxHLlmc] [file...]

    Estimate file space usage (default in unit of 512 blocks).
    -a    Show all file sizes
    -H    Follow symlinks on cmdline
    -L    Follow all symlinks
    -k    Show size in units of 1024.
    -s    Show only the total Size for each file specified
    -x    Estimate size only on the same device
    -c    Print total size of all arguments
    -d N  Limit output to directories (and files with -a) of depth < N
    -l    Count sizes many times if hard linked
    -h    Sizes in human readable format (e.g., 1K 243M 2G )
    -m    Sizes in megabytes
*/

#define FOR_du
#include "toys.h"

GLOBALS(
  long maxdepth;
  long depth;
  long *dirsum;
  long total;
  dev_t st_dev;
  struct arg_list *inodes;
)

typedef struct node_size {
  struct dirtree *node;
  long size;
}node_size;

typedef struct inode_ent {
  ino_t ino;
  dev_t dev;
}inode_ent_t;

/*
 * Adding '/' to the name if name is '.' or '..'
 */

char *make_pathproper(char *str)
{
  char *path = str;
  switch(strlen(str)) {
    case 1:
      if(str[0] == '.') path = xstrdup("./");
      break;
    case 2:
      if(str[0] == '.' && str[1] == '.') path = xstrdup("../");
      break;
    default:
      break;
  }
  return path;
}

/*
 * Print the size of the given entry in specified format, default in blocks of 512 bytes
 */
void print(long size, char* name)
{
  unsigned long long tempsize = (unsigned long long)size * 512;
  unsigned long unit = 512;
  char *sizestr = NULL;
  if(TT.depth > TT.maxdepth) return;
  if(toys.optflags & FLAG_h) unit = 0;
  if(toys.optflags & FLAG_k) unit = 1024;
  if(toys.optflags & FLAG_m) unit = 1024*1024;
  sizestr =  make_human_readable(tempsize, unit); //make human readable string, depending upon unit size.
  xprintf("%s\t%s\n",sizestr, name);
  free(sizestr);
}

/*
 * free the inodes which are stored for hard link reference
 */
void free_inodes(void *data)
{
  void *arg = ((struct arg_list*)data)->arg;
  if(arg) free(arg);
  free(data);
}

/*
 * allocate and add a node to the list
 */
static void llist_add_inode(struct arg_list **old, void *data)
{
  struct arg_list *new = xmalloc(sizeof(struct arg_list));

  new->arg = (char*)data;
  new->next = *old;
  *old = new;
}

/*
 * check if the given stat entry is already there in list or not
 */
int is_inode_present(struct stat *st)
{
  struct arg_list *temparg = NULL;
  inode_ent_t *ent = NULL;
  if(!TT.inodes) return 0;
  for(temparg = TT.inodes; temparg; temparg = (struct arg_list *)temparg->next) {
    ent = (inode_ent_t*)temparg->arg;
    if(ent && ent->ino == st->st_ino && ent->dev == st->st_dev) return 1;
  }
  return 0;
}

/*
 * Compute the size of the node
 */
int do_du(struct dirtree *node)
{
  inode_ent_t *ino_details = NULL;
  node_size *nd = NULL;
  if(!dirtree_notdotdot(node)) return 0;
  if((toys.optflags & FLAG_x) && (TT.st_dev != node->st.st_dev)) //if file not on same device, don't count size
    return DIRTREE_RECURSE;

  if(!(toys.optflags & FLAG_l) && node->st.st_nlink > 1 && !node->extra) { //keeping reference for hard links
    if(is_inode_present(&node->st)) return DIRTREE_RECURSE;
    ino_details = xzalloc(sizeof(inode_ent_t));
    ino_details->ino = node->st.st_ino;
    ino_details->dev = node->st.st_dev;
    llist_add_inode(&TT.inodes, (void*)ino_details);
  }

  if(S_ISDIR(node->st.st_mode)) {
    if(!(node->extra && (long)((node_size*)(node->extra))->node == (long)node)) {
      nd = xzalloc(sizeof(node_size));
      nd->node = node;
      nd->size = 0;
      TT.dirsum = (long*)&(nd->size);
      node->extra = (long)nd;
      *TT.dirsum = 0;
      TT.depth++;
      return (DIRTREE_RECURSE|DIRTREE_COMEAGAIN | ((toys.optflags & FLAG_L) ? DIRTREE_SYMFOLLOW : 0)); //DIRTREE_COMEAGAIN to comeback and print the entry.
    }
    else if(node->extra) { //extra is set for a returning DIR entry.
      long offset = 0;
      nd = (node_size*)node->extra;
      offset = nd->size;
      nd->size += node->st.st_blocks;
      TT.depth--;
      if(!(toys.optflags & FLAG_s))
        print(*TT.dirsum, dirtree_path(node, NULL));
      if((node->parent) && (node->parent->extra)) {
        /* when returning from internal directory, get the saved size of the parent and continue from there */
        nd = (node_size*)node->parent->extra;
        TT.dirsum = (long*)&(nd->size);
        *TT.dirsum += offset;
        *TT.dirsum += node->st.st_blocks;
        return DIRTREE_RECURSE;
      }
      else if(!node->parent) {
        /*if node has no parent, it means it is the top in the tree, stop recursing here */
        TT.total += *TT.dirsum;
        if((toys.optflags & FLAG_s))
          print(*TT.dirsum, dirtree_path(node, NULL));
        return 0;
      }
    }
  }
  else if(!(node->parent)) {
    /* this is the file specified on cmdline */
    TT.total += node->st.st_blocks;
    print(node->st.st_blocks, dirtree_path(node, NULL));
    return 0;
  }
  if(TT.dirsum) *TT.dirsum += node->st.st_blocks;
  if(toys.optflags & FLAG_a && !(toys.optflags & FLAG_s))
    print(node->st.st_blocks, dirtree_path(node, NULL));
  return DIRTREE_RECURSE;
}

/*
 * DU utility main function
 */
void du_main(void)
{
  int symfollow = toys.optflags & (FLAG_H | FLAG_L);
  TT.total = 0;
  TT.inodes = NULL;

  if(!(toys.optflags & FLAG_d)) TT.maxdepth = INT_MAX;
  if(toys.optc == 0) toys.optargs[0] = "./";
  while(*toys.optargs) {
    TT.depth = 0;
    char *path = make_pathproper(*toys.optargs);
    struct dirtree *root = dirtree_add_node(0, path, symfollow);
    if(root) {
      TT.st_dev = root->st.st_dev;
      dirtree_handle_callback(root, do_du); // recurse thru the DIR children.
    }
    toys.optargs++;
  }
  if(TT.inodes) llist_traverse(TT.inodes, free_inodes); //free the stored nodes
  if(toys.optflags & FLAG_c) print(TT.total, "total");
}
