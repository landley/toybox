/* ls.c - list files
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/ls.html

USE_LS(NEWTOY(ls, USE_LS_COLOR("(color):;")USE_LS_Z("Z")"goACFHLRSacdfiklmnpqrstux1[-Cxm1][-Cxml][-Cxmo][-Cxmg][-cu][-ftS][-HL]", TOYFLAG_BIN|TOYFLAG_LOCALE))

config LS
  bool "ls"
  default y
  help
    usage: ls [-ACFHLRSacdfiklmnpqrstux1] [directory...]
    list files

    what to show:
    -a	all files including .hidden		-c  use ctime for timestamps
    -d	directory, not contents			-i  inode number
    -k	block sizes in kilobytes		-p  put a '/' after dir names
    -q	unprintable chars as '?'		-s  size (in blocks)
    -u	use access time for timestamps		-A  list all files but . and ..
    -H	follow command line symlinks		-L  follow symlinks
    -R	recursively list files in subdirs	-F  append /dir *exe @sym |FIFO

    output formats:
    -1	list one file per line			-C  columns (sorted vertically)
    -g	like -l but no owner			-l  long (show full details)
    -m	comma separated				-n  like -l but numeric uid/gid
    -o	like -l but no group			-x  columns (horizontal sort)

    sorting (default is alphabetical):
    -f	unsorted	-r  reverse	-t  timestamp	-S  size

config LS_Z
  bool
  default y
  depends on LS && (TOYBOX_SELINUX || TOYBOX_SMACK)
  help
    usage: ls [-Z]

    -Z	security context

config LS_COLOR
  bool "ls --color"
  default y
  depends on LS
  help
    usage: ls --color[=auto]

    --color  device=yellow  symlink=turquoise/red  dir=blue  socket=purple
             files: exe=green  suid=red  suidfile=redback  stickydir=greenback
             =auto means detect if output is a tty.
*/

#define FOR_ls
#include "toys.h"

// test sst output (suid/sticky in ls flaglist)

// ls -lR starts .: then ./subdir:

GLOBALS(
  char *color;

  struct dirtree *files;

  unsigned screen_width;
  int nl_title;
  char uid_buf[12], gid_buf[12];
)

// Does two things: 1) Returns wcwidth(utf8) version of strlen,
// 2) replaces unprintable characters input string with '?' wildcard char.
int strwidth(char *s)
{
  int total = 0, width, len;
  wchar_t c;

  if (!CFG_TOYBOX_I18N) {
    total = strlen(s);
    if (toys.optflags & FLAG_q) for (; *s; s++) if (!isprint(*s)) *s = '?';
  } else while (*s) {
    len = mbrtowc(&c, s, MB_CUR_MAX, 0);
    if (len < 1 || (width = wcwidth(c)) < 0) {
      total++;
      if (toys.optflags & FLAG_q) *s = '?';
      s++;
    } else {
      s += len;
      total += width;
    }
  }

  return total;
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

  sprintf(TT.uid_buf, "%u", (unsigned)uid);
  return pw ? pw->pw_name : TT.uid_buf;
}

static char *getgroupname(gid_t gid)
{
  struct group *gr = getgrgid(gid);

  sprintf(TT.gid_buf, "%u", (unsigned)gid);
  return gr ? gr->gr_name : TT.gid_buf;
}

static int numlen(long long ll)
{
  return snprintf(0, 0, "%llu", ll);
}

// measure/print SELinux/smack security label. (If pad=0, just measure.)
static unsigned seclabel(struct dirtree *dt, int pad)
{
  if (CFG_TOYBOX_SELINUX) {
    char* path = dirtree_path(dt, 0);
    char* label = 0;
    size_t len;

    lgetfilecon(path, &label);
    if (!label) {
      label = strdup("?");
    }

    len = strlen(label);
    if (pad) printf(" %*s "+(pad>0), pad, label);

    free(label);
    free(path);
    return len;
  } else if (CFG_TOYBOX_SMACK) {
    int fd = openat(dirtree_parentfd(dt), dt->name, O_PATH|O_NOFOLLOW);
    char buf[SMACK_LABEL_LEN+1];
    ssize_t len = 1;

    strcpy(buf, "?");
    if (fd != -1) {
      len = fgetxattr(fd, XATTR_NAME_SMACK, pad?buf:0, pad?SMACK_LABEL_LEN:0);
      close(fd);

      if (len<1 || len>SMACK_LABEL_LEN) len = 0;
      else buf[len] = 0;
    }
    if (pad) printf(" %*s "+(pad>0), pad, buf);

    return len;
  }
}

// Figure out size of printable entry fields for display indent/wrap

static void entrylen(struct dirtree *dt, unsigned *len)
{
  struct stat *st = &(dt->st);
  unsigned flags = toys.optflags;

  *len = strwidth(dt->name);
  if (endtype(st)) ++*len;
  if (flags & FLAG_m) ++*len;

  len[1] = (flags & FLAG_i) ? numlen(st->st_ino) : 0;
  if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) {
    unsigned fn = flags & FLAG_n;
    len[2] = numlen(st->st_nlink);
    len[3] = fn ? numlen(st->st_uid) : strwidth(getusername(st->st_uid));
    len[4] = fn ? numlen(st->st_gid) : strwidth(getgroupname(st->st_gid));
    if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
      // cheating slightly here: assuming minor is always 3 digits to avoid
      // tracking another column
      len[5] = numlen(major(st->st_rdev))+5;
    } else len[5] = numlen(st->st_size);
  }

  len[6] = (flags & FLAG_s) ? numlen(st->st_blocks) : 0;
  len[7] = (CFG_LS_Z && (flags & FLAG_Z)) ? seclabel(dt, 0) : 0;
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

int color_from_mode(mode_t mode)
{
  int color = 0;

  if (S_ISDIR(mode)) color = 256+34;
  else if (S_ISLNK(mode)) color = 256+36;
  else if (S_ISBLK(mode) || S_ISCHR(mode)) color = 256+33;
  else if (S_ISREG(mode) && (mode&0111)) color = 256+32;
  else if (S_ISFIFO(mode)) color = 33;
  else if (S_ISSOCK(mode)) color = 256+35;

  return color;
}

// Display a list of dirtree entries, according to current format
// Output types -1, -l, -C, or stream

static void listfiles(int dirfd, struct dirtree *indir)
{
  struct dirtree *dt, **sort;
  unsigned long dtlen, ul = 0;
  unsigned width, flags = toys.optflags, totals[8], len[8], totpad = 0,
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
    dirtree_recurse(indir, filter, DIRTREE_SYMFOLLOW*!!(flags&FLAG_L));
  }

  // Copy linked list to array and sort it. Directories go in array because
  // we visit them in sorted order too. (The nested loops let us measure and
  // fill with the same inner loop.)
  for (sort = 0;;sort = xmalloc(dtlen*sizeof(void *))) {
    for (dtlen = 0, dt = indir->child; dt; dt = dt->next, dtlen++)
      if (sort) sort[dtlen] = dt;
    if (sort) break;
  }

  // Label directory if not top of tree, or if -R
  if (indir->parent && (!indir->extra || (flags & FLAG_R)))
  {
    char *path = dirtree_path(indir, 0);

    if (TT.nl_title++) xputc('\n');
    xprintf("%s:\n", path);
    free(path);
  }

  // Measure each entry to work out whitespace padding and total blocks
  if (!(flags & FLAG_f)) {
    unsigned long long blocks = 0;

    qsort(sort, dtlen, sizeof(void *), (void *)compare);
    for (ul = 0; ul<dtlen; ul++) {
      entrylen(sort[ul], len);
      for (width = 0; width<8; width++)
        if (len[width]>totals[width]) totals[width] = len[width];
      blocks += sort[ul]->st.st_blocks;
    }
    totpad = totals[1]+!!totals[1]+totals[6]+!!totals[6]+totals[7]+!!totals[7];
    if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g|FLAG_s) && indir->parent)
      xprintf("total %llu\n", blocks);
  }

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
        *len += totpad;
        if (c == columns) break;
        // Expand this column if necessary, break if that puts us over budget
        if (*len > colsizes[c]) {
          totlen += (*len)-colsizes[c];
          colsizes[c] = *len;
          if (totlen > TT.screen_width) break;
        }
      }
      // If everything fit, stop here
      if (ul == dtlen) break;
    }
  }

  // Loop through again to produce output.
  memset(toybuf, ' ', 256);
  width = 0;
  for (ul = 0; ul<dtlen; ul++) {
    unsigned curcol, color = 0;
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

    if (flags & FLAG_i)
      xprintf("%*lu ", totals[1], (unsigned long)st->st_ino);
    if (flags & FLAG_s)
      xprintf("%*lu ", totals[6], (unsigned long)st->st_blocks);

    if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) {
      struct tm *tm;
      char perm[11], thyme[64], *usr, *upad, *grp, *grpad;

      mode_to_string(mode, perm);

      if (flags&FLAG_o) grp = grpad = toybuf+256;
      else {
        if (flags&FLAG_n) sprintf(grp = thyme, "%u", (unsigned)st->st_gid);
        else strwidth(grp = getgroupname(st->st_gid));
        grpad = toybuf+256-(totals[4]-len[4]);
      }

      if (flags&FLAG_g) usr = upad = toybuf+256;
      else {
        upad = toybuf+255-(totals[3]-len[3]);
        if (flags&FLAG_n) sprintf(usr = TT.uid_buf, "%u", (unsigned)st->st_uid);
        else strwidth(usr = getusername(st->st_uid));
      }

      // Coerce the st types into something we know we can print.
      printf("%s% *ld %s%s%s%s", perm, totals[2]+1, (long)st->st_nlink,
             usr, upad, grp, grpad);

      if (CFG_LS_Z && (flags & FLAG_Z)) seclabel(sort[next], -(int)totals[7]);

      if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
        printf("% *d,% 4d", totals[5]-4, major(st->st_rdev),minor(st->st_rdev));
      else printf("% *lld", totals[5]+1, (long long)st->st_size);

      tm = localtime(&(st->st_mtime));
      strftime(thyme, sizeof(thyme), "%F %H:%M", tm);
      xprintf(" %s ", thyme);
    } else if (CFG_LS_Z && (flags & FLAG_Z)) seclabel(sort[next], totals[7]);

    if (flags & FLAG_color) {
      color = color_from_mode(st->st_mode);
      if (color) printf("\033[%d;%dm", color>>8, color&255);
    }

    if (flags & FLAG_q) {
      char *p;
      for (p=sort[next]->name; *p; p++) fputc(isprint(*p) ? *p : '?', stdout);
    } else xprintf("%s", sort[next]->name);
    if (color) xprintf("\033[0m");

    if ((flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) && S_ISLNK(mode)) {
      printf(" -> ");
      if (flags & FLAG_color) {
        struct stat st2;

        if (fstatat(dirfd, sort[next]->symlink, &st2, 0)) color = 256+31;
        else color = color_from_mode(st2.st_mode);

        if (color) printf("\033[%d;%dm", color>>8, color&255);
      }

      printf("%s", sort[next]->symlink);
      if (color) printf("\033[0m");
    }

    if (et) xputc(et);

    // Pad columns
    if (flags & (FLAG_C|FLAG_x)) {
      curcol = colsizes[curcol]-(*len)-totpad;
      if (curcol < 255) xprintf("%s", toybuf+255-curcol);
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
  if (dirfd != AT_FDCWD) close(dirfd);
}

void ls_main(void)
{
  char **s, *noargs[] = {".", 0};
  struct dirtree *dt;

  TT.screen_width = 80;
  terminal_size(&TT.screen_width, NULL);
  if (TT.screen_width<2) TT.screen_width = 2;

  // Do we have an implied -1
  if (!isatty(1)) {
    toys.optflags |= FLAG_1;
    if (TT.color) toys.optflags ^= FLAG_color;
  } else if (toys.optflags&(FLAG_l|FLAG_o|FLAG_n|FLAG_g))
    toys.optflags |= FLAG_1;
  else if (!(toys.optflags&(FLAG_1|FLAG_x|FLAG_m))) toys.optflags |= FLAG_C;
  // The optflags parsing infrastructure should really do this for us,
  // but currently it has "switch off when this is set", so "-dR" and "-Rd"
  // behave differently
  if (toys.optflags & FLAG_d) toys.optflags &= ~FLAG_R;

  // Iterate through command line arguments, collecting directories and files.
  // Non-absolute paths are relative to current directory.
  TT.files = dirtree_start(0, 0);
  for (s = *toys.optargs ? toys.optargs : noargs; *s; s++) {
    dt = dirtree_start(*s, !(toys.optflags&(FLAG_l|FLAG_d|FLAG_F)) ||
                            (toys.optflags&(FLAG_L|FLAG_H)));

    // note: double_list->prev temporarirly goes in dirtree->parent
    if (dt) dlist_add_nomalloc((void *)&TT.files->child, (void *)dt);
    else toys.exitval = 1;
  }

  // Convert double_list into dirtree.
  dlist_terminate(TT.files->child);
  for (dt = TT.files->child; dt; dt = dt->next) dt->parent = TT.files;

  // Display the files we collected
  listfiles(AT_FDCWD, TT.files);

  if (CFG_TOYBOX_FREE) free(TT.files);
}
