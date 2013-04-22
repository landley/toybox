/* ls.c - list files
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/ls.html

USE_LS(NEWTOY(ls, "goACFHLRSacdfiklmnpqrstux1[-1Cglmnox][-cu][-ftS][-HL]", TOYFLAG_BIN))

config LS
  bool "ls"
  default y
  help
    usage: ls [-ACFHLRSacdfiklmnpqrstux1] [directory...]
    list files

    what to show:
    -a	all files including .hidden
    -c	use ctime for timestamps
    -d	directory, not contents
    -i	inode number
    -k	block sizes in kilobytes
    -p	put a '/' after directory names
    -q	unprintable chars as '?'
    -s	size (in blocks)
    -u	use access time for timestamps
    -A	list all files except . and ..
    -H	follow command line symlinks
    -L	follow symlinks
    -R	recursively list files in subdirectories
    -F	append file type indicator (/=dir, *=exe, @=symlink, |=FIFO)

    output formats:
    -1	list one file per line
    -C	columns (sorted vertically)
    -g	like -l but no owner
    -l	long (show full details for each file)
    -m	comma separated
    -n	like -l but numeric uid/gid
    -o	like -l but no group
    -x	columns (sorted horizontally)

    sorting (default is alphabetical):
    -f	unsorted
    -r	reverse
    -t	timestamp
    -S	size
*/

#define FOR_ls
#include "toys.h"

// test sst output (suid/sticky in ls flaglist)

// ls -lR starts .: then ./subdir:

GLOBALS(
  struct dirtree *files;

  unsigned screen_width;
  int nl_title;

  // group and user can make overlapping use of the utoa() buf, so move it
  char uid_buf[12];
)

void dlist_to_dirtree(struct dirtree *parent)
{
  // Turn double_list into dirtree
  struct dirtree *dt = parent->child;
  if (dt) {
    dt->parent->next = NULL;
    while (dt) {
      dt->parent = parent;
      dt = dt->next;
    }
  }
}

static char endtype(struct stat *st)
{
  mode_t mode = st->st_mode;
  if ((toys.optflags&(FLAG_F|FLAG_p)) && S_ISDIR(mode)) return '/';
  if (toys.optflags & FLAG_F) {
    if (S_ISLNK(mode)) return '@';
    if (S_ISREG(mode) && (mode&0111)) return '*';
    if (S_ISFIFO(mode)) return '|';
    if (S_ISSOCK(mode)) return '=';
  }
  return 0;
}

static char *getusername(uid_t uid)
{
  struct passwd *pw = getpwuid(uid);
  utoa_to_buf(uid, TT.uid_buf, 12);
  return pw ? pw->pw_name : TT.uid_buf;
}

static char *getgroupname(gid_t gid)
{
  struct group *gr = getgrgid(gid);
  return gr ? gr->gr_name : utoa(gid);
}

// Figure out size of printable entry fields for display indent/wrap

static void entrylen(struct dirtree *dt, unsigned *len)
{
  struct stat *st = &(dt->st);
  unsigned flags = toys.optflags;

  *len = strlen(dt->name);
  if (endtype(st)) ++*len;
  if (flags & FLAG_m) ++*len;

  if (flags & FLAG_i) *len += (len[1] = numlen(st->st_ino));
  if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) {
    unsigned fn = flags & FLAG_n;
    len[2] = numlen(st->st_nlink);
    len[3] = strlen(fn ? utoa(st->st_uid) : getusername(st->st_uid));
    len[4] = strlen(fn ? utoa(st->st_gid) : getgroupname(st->st_gid));
    len[5] = numlen(st->st_size);
  }
  if (flags & FLAG_s) *len += (len[6] = numlen(st->st_blocks));
}

static int compare(void *a, void *b)
{
  struct dirtree *dta = *(struct dirtree **)a;
  struct dirtree *dtb = *(struct dirtree **)b;
  int ret = 0, reverse = (toys.optflags & FLAG_r) ? -1 : 1;

  if (toys.optflags & FLAG_S) {
    if (dta->st.st_size > dtb->st.st_size) ret = -1;
    else if (dta->st.st_size < dtb->st.st_size) ret = 1;
  }
  if (toys.optflags & FLAG_t) {
    if (dta->st.st_mtime > dtb->st.st_mtime) ret = -1;
    else if (dta->st.st_mtime < dtb->st.st_mtime) ret = 1;
  }
  if (!ret) ret = strcmp(dta->name, dtb->name);
  return ret * reverse;
}

// callback from dirtree_recurse() determining how to handle this entry.

static int filter(struct dirtree *new)
{
  int flags = toys.optflags;

  // Special case to handle enormous dirs without running out of memory.
  if (flags == (FLAG_1|FLAG_f)) {
    xprintf("%s\n", new->name);
    return 0;
  }

  if (flags & FLAG_u) new->st.st_mtime = new->st.st_atime;
  if (flags & FLAG_c) new->st.st_mtime = new->st.st_ctime;
  if (flags & FLAG_k) new->st.st_blocks = (new->st.st_blocks + 1) / 2;

  if (flags & (FLAG_a|FLAG_f)) return DIRTREE_SAVE;
  if (!(flags & FLAG_A) && new->name[0]=='.') return 0;

  return dirtree_notdotdot(new) & DIRTREE_SAVE;
}

// For column view, calculate horizontal position (for padding) and return
// index of next entry to display.

static unsigned long next_column(unsigned long ul, unsigned long dtlen,
  unsigned columns, unsigned *xpos)
{
  unsigned long transition;
  unsigned height, widecols;

  // Horizontal sort is easy
  if (!(toys.optflags & FLAG_C)) {
    *xpos = ul % columns;
    return ul;
  }

  // vertical sort

  // For -x, calculate height of display, rounded up
  height = (dtlen+columns-1)/columns;

  // Sanity check: does wrapping render this column count impossible
  // due to the right edge wrapping eating a whole row?
  if (height*columns - dtlen >= height) {
    *xpos = columns;
    return 0;
  }

  // Uneven rounding goes along right edge
  widecols = dtlen % height;
  if (!widecols) widecols = height;
  transition = widecols * columns;
  if (ul < transition) {
    *xpos =  ul % columns;
    return (*xpos*height) + (ul/columns);
  }

  ul -= transition;
  *xpos = ul % (columns-1);

  return (*xpos*height) + widecols + (ul/(columns-1));
}

// Display a list of dirtree entries, according to current format
// Output types -1, -l, -C, or stream

static void listfiles(int dirfd, struct dirtree *indir)
{
  struct dirtree *dt, **sort = 0;
  unsigned long dtlen = 0, ul = 0;
  unsigned width, flags = toys.optflags, totals[7], len[7],
    *colsizes = (unsigned *)(toybuf+260), columns = (sizeof(toybuf)-260)/4;

  memset(totals, 0, sizeof(totals));

  // Silently descend into single directory listed by itself on command line.
  // In this case only show dirname/total header when given -R.
  if (!indir->parent) {
    if (!(dt = indir->child)) return;
    if (S_ISDIR(dt->st.st_mode) && !dt->next && !(flags & FLAG_d)) {
      dt->extra = 1;
      listfiles(open(dt->name, 0), dt);
      return;
    }
  } else {
    // Read directory contents. We dup() the fd because this will close it.
    indir->data = dup(dirfd);
    dirtree_recurse(indir, filter, (flags&FLAG_L));
  }

  // Copy linked list to array and sort it. Directories go in array because
  // we visit them in sorted order.

  for (;;) {
    for (dt = indir->child; dt; dt = dt->next) {
      if (sort) sort[dtlen] = dt;
      dtlen++;
    }
    if (sort) break;
    sort = xmalloc(dtlen * sizeof(void *));
    dtlen = 0;
    continue;
  }

  // Label directory if not top of tree, or if -R
  if (indir->parent && (!indir->extra || (flags & FLAG_R)))
  {
    char *path = dirtree_path(indir, 0);

    if (TT.nl_title++) xputc('\n');
    xprintf("%s:\n", path);
    free(path);
  }

  if (!(flags & FLAG_f)) qsort(sort, dtlen, sizeof(void *), (void *)compare);

  // Find largest entry in each field for display alignment
  if (flags & (FLAG_C|FLAG_x)) {

    // columns can't be more than toybuf can hold, or more than files,
    // or > 1/2 screen width (one char filename, one space).
    if (columns > TT.screen_width/2) columns = TT.screen_width/2;
    if (columns > dtlen) columns = dtlen;

    // Try to fit as many columns as we can, dropping down by one each time
    for (;columns > 1; columns--) {
      unsigned c, totlen = columns;

      memset(colsizes, 0, columns*sizeof(unsigned));
      for (ul=0; ul<dtlen; ul++) {
        entrylen(sort[next_column(ul, dtlen, columns, &c)], len);
        if (c == columns) break;
        // Does this put us over budget?
        if (*len > colsizes[c]) {
          totlen += *len-colsizes[c];
          colsizes[c] = *len;
          if (totlen > TT.screen_width) break;
        }
      }
      // If it fit, stop here
      if (ul == dtlen) break;
    }
  } else if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g|FLAG_s)) {
    unsigned long blocks = 0;

    for (ul = 0; ul<dtlen; ul++)
    {
      entrylen(sort[ul], len);
      for (width=0; width<6; width++)
        if (len[width] > totals[width]) totals[width] = len[width];
      blocks += sort[ul]->st.st_blocks;
    }

    if (indir->parent) xprintf("total %lu\n", blocks);
  }

  // Loop through again to produce output.
  memset(toybuf, ' ', 256);
  width = 0;
  for (ul = 0; ul<dtlen; ul++) {
    unsigned curcol;
    unsigned long next = next_column(ul, dtlen, columns, &curcol);
    struct stat *st = &(sort[next]->st);
    mode_t mode = st->st_mode;
    char et = endtype(st);

    // Skip directories at the top of the tree when -d isn't set
    if (S_ISDIR(mode) && !indir->parent && !(flags & FLAG_d)) continue;
    TT.nl_title=1;

    // Handle padding and wrapping for display purposes
    entrylen(sort[next], len);
    if (ul) {
      if (flags & FLAG_m) xputc(',');
      if (flags & (FLAG_C|FLAG_x)) {
        if (!curcol) xputc('\n');
      } else if ((flags & FLAG_1) || width+1+*len > TT.screen_width) {
        xputc('\n');
        width = 0;
      } else {
        xputc(' ');
        width++;
      }
    }
    width += *len;

    if (flags & FLAG_i) xprintf("% *lu ", len[1], (unsigned long)st->st_ino);
    if (flags & FLAG_s) xprintf("% *lu ", len[6], (unsigned long)st->st_blocks);

    if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) {
      struct tm *tm;
      char perm[11], thyme[64], *usr, *upad, *grp, *grpad;

      format_mode(&perm, mode);

      tm = localtime(&(st->st_mtime));
      strftime(thyme, sizeof(thyme), "%F %H:%M", tm);

      if (flags&FLAG_o) grp = grpad = toybuf+256;
      else {
        grp = (flags&FLAG_n) ? utoa(st->st_gid) : getgroupname(st->st_gid);
        grpad = toybuf+256-(totals[4]-len[4]);
      }

      if (flags&FLAG_g) usr = upad = toybuf+256;
      else {
        upad = toybuf+255-(totals[3]-len[3]);
        if (flags&FLAG_n) {
          usr = TT.uid_buf;
          utoa_to_buf(st->st_uid, TT.uid_buf, 12);
        } else usr = getusername(st->st_uid);
      }

      // Coerce the st types into something we know we can print.
      xprintf("%s% *ld %s%s%s%s% *"PRId64" %s ", perm, totals[2]+1,
        (long)st->st_nlink, usr, upad, grp, grpad, totals[5]+1,
        (int64_t)st->st_size, thyme);
    }

    if (flags & FLAG_q) {
      char *p;
      for (p=sort[next]->name; *p; p++) xputc(isprint(*p) ? *p : '?');
    } else xprintf("%s", sort[next]->name);
    if ((flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) && S_ISLNK(mode))
      xprintf(" -> %s", sort[next]->symlink);

    if (et) xputc(et);

    // Pad columns
    if (flags & (FLAG_C|FLAG_x)) {
      curcol = colsizes[curcol] - *len;
      if (curcol >= 0) xprintf("%s", toybuf+255-curcol);
    }
  }

  if (width) xputc('\n');

  // Free directory entries, recursing first if necessary.

  for (ul = 0; ul<dtlen; free(sort[ul++])) {
    if ((flags & FLAG_d) || !S_ISDIR(sort[ul]->st.st_mode)
      || !dirtree_notdotdot(sort[ul])) continue;

    // Recurse into dirs if at top of the tree or given -R
    if (!indir->parent || (flags & FLAG_R))
      listfiles(openat(dirfd, sort[ul]->name, 0), sort[ul]);
  }
  free(sort);
  if (dirfd != AT_FDCWD) close(indir->data);
}

void ls_main(void)
{
  char **s, *noargs[] = {".", 0};
  struct dirtree *dt;

  // Do we have an implied -1
  if (!isatty(1) || (toys.optflags&(FLAG_l|FLAG_o|FLAG_n|FLAG_g)))
    toys.optflags |= FLAG_1;
  else {
    TT.screen_width = 80;
    terminal_size(&TT.screen_width, NULL);
    if (TT.screen_width<2) TT.screen_width = 2;
    if (!(toys.optflags&(FLAG_1|FLAG_x|FLAG_m))) toys.optflags |= FLAG_C;
  }
  // The optflags parsing infrastructure should really do this for us,
  // but currently it has "switch off when this is set", so "-dR" and "-Rd"
  // behave differently
  if (toys.optflags & FLAG_d) toys.optflags &= ~FLAG_R;

  // Iterate through command line arguments, collecting directories and files.
  // Non-absolute paths are relative to current directory.
  TT.files = dirtree_add_node(0, 0, 0);
  for (s = *toys.optargs ? toys.optargs : noargs; *s; s++) {
    dt = dirtree_add_node(0, *s,
      (toys.optflags & (FLAG_L|FLAG_H|FLAG_l))^FLAG_l);

    if (!dt) {
      toys.exitval = 1;
      continue;
    }

    // Typecast means double_list->prev temporarirly goes in dirtree->parent
    dlist_add_nomalloc((void *)&TT.files->child, (struct double_list *)dt);
  }

  // Turn double_list into dirtree
  dlist_to_dirtree(TT.files);

  // Display the files we collected
  listfiles(AT_FDCWD, TT.files);

  if (CFG_TOYBOX_FREE) free(TT.files);
}
