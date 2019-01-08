/* ls.c - list files
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/ls.html
 *
 * Deviations from posix:
 *   add -b (and default to it instead of -q for an unambiguous representation
 *   that doesn't cause collisions)
 *   add -Z -ll --color
 *   Posix says the -l date format should vary based on how recent it is
 *   and we do --time-style=long-iso instead

USE_LS(NEWTOY(ls, "(color):;(full-time)(show-control-chars)ZgoACFHLRSabcdfhikl@mnpqrstux1[-Cxm1][-Cxml][-Cxmo][-Cxmg][-cu][-ftS][-HL][!qb]", TOYFLAG_BIN|TOYFLAG_LOCALE))

config LS
  bool "ls"
  default y
  help
    usage: ls [-ACFHLRSZacdfhiklmnpqrstux1] [--color[=auto]] [directory...]

    list files

    what to show:
    -a  all files including .hidden    -b  escape nongraphic chars
    -c  use ctime for timestamps       -d  directory, not contents
    -i  inode number                   -p  put a '/' after dir names
    -q  unprintable chars as '?'       -s  storage used (1024 byte units)
    -u  use access time for timestamps -A  list all files but . and ..
    -H  follow command line symlinks   -L  follow symlinks
    -R  recursively list in subdirs    -F  append /dir *exe @sym |FIFO
    -Z  security context

    output formats:
    -1  list one file per line         -C  columns (sorted vertically)
    -g  like -l but no owner           -h  human readable sizes
    -l  long (show full details)       -m  comma separated
    -n  like -l but numeric uid/gid    -o  like -l but no group
    -x  columns (horizontal sort)      -ll long with nanoseconds (--full-time)
    --color  device=yellow  symlink=turquoise/red  dir=blue  socket=purple
             files: exe=green  suid=red  suidfile=redback  stickydir=greenback
             =auto means detect if output is a tty.

    sorting (default is alphabetical):
    -f  unsorted    -r  reverse    -t  timestamp    -S  size
*/

#define FOR_ls
#include "toys.h"

// test sst output (suid/sticky in ls flaglist)

// ls -lR starts .: then ./subdir:

GLOBALS(
  long l;
  char *color;

  struct dirtree *files, *singledir;
  unsigned screen_width;
  int nl_title;
  char *escmore;
)

// Callback from crunch_str to represent unprintable chars
static int crunch_qb(FILE *out, int cols, int wc)
{
  unsigned len = 1;
  char buf[32];

  if (toys.optflags&FLAG_q) *buf = '?';
  else {
    if (wc<256) *buf = wc;
    // scrute the inscrutable, eff the ineffable, print the unprintable
    else len = wcrtomb(buf, wc, 0);
    if (toys.optflags&FLAG_b) {
      char *to = buf, *from = buf+24;
      int i, j;

      memcpy(from, to, 8);
      for (i = 0; i<len; i++) {
        *to++ = '\\';
        if (strchr(TT.escmore, from[i])) *to++ = from[i];
        else if (-1 != (j = stridx("\\\a\b\033\f\n\r\t\v", from[i])))
          *to++ = "\\abefnrtv"[j];
        else to += sprintf(to, "%03o", from[i]);
      }
      len = to-buf;
    }
  }

  if (cols<len) len = cols;
  if (out) fwrite(buf, len, 1, out);

  return len;
}

// Returns wcwidth(utf8) version of strlen with -qb escapes
static int strwidth(char *s)
{
  return crunch_str(&s, INT_MAX, 0, TT.escmore, crunch_qb);
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

static int numlen(long long ll)
{
  return snprintf(0, 0, "%llu", ll);
}

static int print_with_h(char *s, long long value, int units)
{
  if (toys.optflags&FLAG_h) return human_readable(s, value*units, 0);
  else return sprintf(s, "%lld", value);
}

// Figure out size of printable entry fields for display indent/wrap

static void entrylen(struct dirtree *dt, unsigned *len)
{
  struct stat *st = &(dt->st);
  unsigned flags = toys.optflags;
  char tmp[64];

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
      len[5] = numlen(dev_major(st->st_rdev))+5;
    } else len[5] = print_with_h(tmp, st->st_size, 1);
  }

  len[6] = (flags & FLAG_s) ? print_with_h(tmp, st->st_blocks, 512) : 0;
  len[7] = (flags & FLAG_Z) ? strwidth((char *)dt->extra) : 0;
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
    else if (dta->st.st_mtim.tv_nsec > dtb->st.st_mtim.tv_nsec) ret = -1;
    else if (dta->st.st_mtim.tv_nsec < dtb->st.st_mtim.tv_nsec) ret = 1;
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

  if (flags & FLAG_Z) {
    if (!CFG_TOYBOX_LSM_NONE) {

      // (Wouldn't it be nice if the lsm functions worked like openat(),
      // fchmodat(), mknodat(), readlinkat() so we could do this without
      // even O_PATH? But no, this is 1990's tech.)
      int fd = openat(dirtree_parentfd(new), new->name,
        O_PATH|(O_NOFOLLOW*!(toys.optflags&FLAG_L)));

      if (fd != -1) {
        if (-1 == lsm_fget_context(fd, (char **)&new->extra) && errno == EBADF)
        {
          char hack[32];

          // Work around kernel bug that won't let us read this "metadata" from
          // the filehandle unless we have permission to read the data. (We can
          // query the same data in by path, but can't do it through an O_PATH
          // filehandle, because reasons. But for some reason, THIS is ok? If
          // they ever fix the kernel, this should stop triggering.)

          sprintf(hack, "/proc/self/fd/%d", fd);
          lsm_lget_context(hack, (char **)&new->extra);
        }
        close(fd);
      }
    }
    if (CFG_TOYBOX_LSM_NONE || !new->extra) new->extra = (long)xstrdup("?");
  }

  if (flags & FLAG_u) new->st.st_mtime = new->st.st_atime;
  if (flags & FLAG_c) new->st.st_mtime = new->st.st_ctime;
  new->st.st_blocks >>= 1;

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

static int color_from_mode(mode_t mode)
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
    *colsizes = (unsigned *)toybuf, columns = sizeof(toybuf)/4;
  char tmp[64];

  if (-1 == dirfd) {
    perror_msg_raw(indir->name);

    return;
  }

  memset(totals, 0, sizeof(totals));
  if (CFG_TOYBOX_DEBUG) memset(len, 0, sizeof(len));

  // Top level directory was already populated by main()
  if (!indir->parent) {
    // Silently descend into single directory listed by itself on command line.
    // In this case only show dirname/total header when given -R.
    dt = indir->child;
    if (dt && S_ISDIR(dt->st.st_mode) && !dt->next && !(flags&(FLAG_d|FLAG_R)))
    {
      listfiles(open(dt->name, 0), TT.singledir = dt);

      return;
    }

    // Do preprocessing (Dirtree didn't populate, so callback wasn't called.)
    for (;dt; dt = dt->next) filter(dt);
    if (flags == (FLAG_1|FLAG_f)) return;
  // Read directory contents. We dup() the fd because this will close it.
  // This reads/saves contents to display later, except for in "ls -1f" mode.
  } else dirtree_recurse(indir, filter, dup(dirfd),
      DIRTREE_SYMFOLLOW*!!(flags&FLAG_L));

  // Copy linked list to array and sort it. Directories go in array because
  // we visit them in sorted order too. (The nested loops let us measure and
  // fill with the same inner loop.)
  for (sort = 0;;sort = xmalloc(dtlen*sizeof(void *))) {
    for (dtlen = 0, dt = indir->child; dt; dt = dt->next, dtlen++)
      if (sort) sort[dtlen] = dt;
    if (sort || !dtlen) break;
  }

  // Label directory if not top of tree, or if -R
  if (indir->parent && (TT.singledir!=indir || (flags&FLAG_R)))
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
    if ((flags&(FLAG_h|FLAG_l|FLAG_o|FLAG_n|FLAG_g|FLAG_s)) && indir->parent) {
      print_with_h(tmp, blocks, 512);
      xprintf("total %s\n", tmp);
    }
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
        *len += totpad+1;
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
  width = 0;
  for (ul = 0; ul<dtlen; ul++) {
    int ii;
    unsigned curcol, color = 0;
    unsigned long next = next_column(ul, dtlen, columns, &curcol);
    struct stat *st = &(sort[next]->st);
    mode_t mode = st->st_mode;
    char et = endtype(st), *ss;

    // Skip directories at the top of the tree when -d isn't set
    if (S_ISDIR(mode) && !indir->parent && !(flags & FLAG_d)) continue;
    TT.nl_title=1;

    // Handle padding and wrapping for display purposes
    entrylen(sort[next], len);
    if (ul) {
      int mm = !!(flags & FLAG_m);

      if (mm) xputc(',');
      if (flags & (FLAG_C|FLAG_x)) {
        if (!curcol) xputc('\n');
      } else if ((flags & FLAG_1) || width+1+*len > TT.screen_width) {
        xputc('\n');
        width = 0;
      } else {
        printf("  "+mm, 0); // shut up the stupid compiler
        width += 2-mm;
      }
    }
    width += *len;

    if (flags & FLAG_i) printf("%*lu ", totals[1], (unsigned long)st->st_ino);

    if (flags & FLAG_s) {
      print_with_h(tmp, st->st_blocks, 512);
      printf("%*s ", totals[6], tmp);
    }

    if (flags & (FLAG_l|FLAG_o|FLAG_n|FLAG_g)) {
      struct tm *tm;

      // (long) is to coerce the st types into something we know we can print.
      mode_to_string(mode, tmp);
      printf("%s% *ld", tmp, totals[2]+1, (long)st->st_nlink);

      // print user
      if (!(flags&FLAG_g)) {
        putchar(' ');
        ii = -totals[3];
        if (flags&FLAG_n) printf("%*u", ii, (unsigned)st->st_uid);
        else draw_trim_esc(getusername(st->st_uid), ii, abs(ii), TT.escmore,
                           crunch_qb);
      }

      // print group
      if (!(flags&FLAG_o)) {
        putchar(' ');
        ii = -totals[4];
        if (flags&FLAG_n) printf("%*u", ii, (unsigned)st->st_gid);
        else draw_trim_esc(getgroupname(st->st_gid), ii, abs(ii), TT.escmore,
                           crunch_qb);
      }

      if (flags & FLAG_Z)
        printf(" %-*s", -(int)totals[7], (char *)sort[next]->extra);

      // print major/minor, or size
      if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
        printf("% *d,% 4d", totals[5]-4, dev_major(st->st_rdev),
          dev_minor(st->st_rdev));
      else {
        print_with_h(tmp, st->st_size, 1);
        printf("%*s", totals[5]+1, tmp);
      }

      // print time, always in --time-style=long-iso
      tm = localtime(&(st->st_mtime));
      strftime(tmp, sizeof(tmp), "%F %H:%M", tm);
      if (TT.l>1) {
        char *s = tmp+strlen(tmp);

        s += sprintf(s, ":%02d.%09d ", tm->tm_sec, (int)st->st_mtim.tv_nsec);
        strftime(s, sizeof(tmp)-(s-tmp), "%z", tm);
      }
      printf(" %s ", tmp);
    } else if (flags & FLAG_Z)
      printf("%-*s ", (int)totals[7], (char *)sort[next]->extra);

    if (flags & FLAG_color) {
      color = color_from_mode(st->st_mode);
      if (color) printf("\033[%d;%dm", color>>8, color&255);
    }

    ss = sort[next]->name;
    crunch_str(&ss, INT_MAX, stdout, TT.escmore, crunch_qb);
    if (color) printf("\033[0m");

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
      if (curcol < 255) printf("%*c", curcol, ' ');
    }
  }

  if (width) xputc('\n');

  // Free directory entries, recursing first if necessary.

  for (ul = 0; ul<dtlen; free(sort[ul++])) {
    if ((flags & FLAG_d) || !S_ISDIR(sort[ul]->st.st_mode)) continue;

    // Recurse into dirs if at top of the tree or given -R
    if (!indir->parent || ((flags&FLAG_R) && dirtree_notdotdot(sort[ul])))
      listfiles(openat(dirfd, sort[ul]->name, 0), sort[ul]);
    free((void *)sort[ul]->extra);
  }
  free(sort);
  if (dirfd != AT_FDCWD) close(dirfd);
}

void ls_main(void)
{
  char **s, *noargs[] = {".", 0};
  struct dirtree *dt;

  if (toys.optflags&FLAG_full_time) {
    toys.optflags |= FLAG_l;
    TT.l = 2;
  }

  // Do we have an implied -1
  if (isatty(1)) {
    if (!(toys.optflags&FLAG_show_control_chars)) toys.optflags |= FLAG_b;
    if (toys.optflags&(FLAG_l|FLAG_o|FLAG_n|FLAG_g)) toys.optflags |= FLAG_1;
    else if (!(toys.optflags&(FLAG_1|FLAG_x|FLAG_m))) toys.optflags |= FLAG_C;
  } else {
    if (!(toys.optflags & FLAG_m)) toys.optflags |= FLAG_1;
    if (TT.color) toys.optflags ^= FLAG_color;
  }

  TT.screen_width = 80;
  terminal_size(&TT.screen_width, NULL);
  if (TT.screen_width<2) TT.screen_width = 2;
  if (toys.optflags&FLAG_b) TT.escmore = " \\";

  // The optflags parsing infrastructure should really do this for us,
  // but currently it has "switch off when this is set", so "-dR" and "-Rd"
  // behave differently
  if (toys.optflags & FLAG_d) toys.optflags &= ~FLAG_R;

  // Iterate through command line arguments, collecting directories and files.
  // Non-absolute paths are relative to current directory. Top of tree is
  // a dummy node to collect command line arguments into pseudo-directory.
  TT.files = dirtree_add_node(0, 0, 0);
  TT.files->dirfd = AT_FDCWD;
  for (s = *toys.optargs ? toys.optargs : noargs; *s; s++) {
    int sym = !(toys.optflags&(FLAG_l|FLAG_d|FLAG_F))
      || (toys.optflags&(FLAG_L|FLAG_H));

    dt = dirtree_add_node(0, *s, DIRTREE_SYMFOLLOW*sym);

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
