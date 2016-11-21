/* diff.c - compare files line by line
 *
 * Copyright 2014 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2014 Ashwini Kumar <ak.ashwini1981@gmail.com>
 *
 * See: http://cm.bell-labs.com/cm/cs/cstr/41.pdf

USE_DIFF(NEWTOY(diff, "<2>2B(ignore-blank-lines)d(minimal)b(ignore-space-change)ut(expand-tabs)w(ignore-all-space)i(ignore-case)T(initial-tab)s(report-identical-files)q(brief)a(text)L(label)*S(starting-file):N(new-file)r(recursive)U(unified)#<0=3", TOYFLAG_USR|TOYFLAG_BIN))

config DIFF
  bool "diff"
  default n
  help
  usage: diff [-abBdiNqrTstw] [-L LABEL] [-S FILE] [-U LINES] FILE1 FILE2

  -a  Treat all files as text
  -b  Ignore changes in the amount of whitespace
  -B  Ignore changes whose lines are all blank
  -d  Try hard to find a smaller set of changes
  -i  Ignore case differences
  -L  Use LABEL instead of the filename in the unified header
  -N  Treat absent files as empty
  -q  Output only whether files differ
  -r  Recurse
  -S  Start with FILE when comparing directories
  -T  Make tabs line up by prefixing a tab when necessary
  -s  Report when two files are the same
  -t  Expand tabs to spaces in output
  -U  Output LINES lines of context
  -w  Ignore all whitespace
*/

#define FOR_diff
#include "toys.h"

GLOBALS(
  long ct;
  char *start;
  struct arg_list *L_list;

  int dir_num, size, is_binary, status, change, len[2];
  int *offset[2];
)

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define IS_STDIN(s)     ((s)[0] == '-' && !(s)[1])

struct v_vector {
  unsigned serial:31;
  unsigned last:1;
  union {
    unsigned hash;
    unsigned p;
  };
};

struct diff {
  long a, b, c, d, prev, suff;
};

static struct dir_t {
  char **list;
  int nr_elm;
} dir[2];

struct candidate {
  int a, b;
  struct candidate *prev, *next;
};

static struct file_t {
  FILE *fp;
  int len;
} file[2];

enum {
  SAME,
  DIFFER,
};

enum {
  empty = 1 << 9,
  eol = 1 << 10,
  eof = 1 << 11,
  space = 1 << 12
};

static int comp(const void *a, const void* b)
{
  int i = ((struct v_vector *)a)->hash -
    ((struct v_vector *)b)->hash;

  if (!i) i = ((struct v_vector *)a)->serial -
    ((struct v_vector *)b)->serial;
  return i;
}

static int search (struct candidate **K, int r, int k, int j)
{
  int low = r, upper = k, mid;

  mid = (low + upper) / 2;
  while (low <= mid) {
    if (((struct candidate*)(K[mid]))->b < j &&
        ((struct candidate*)(K[mid + 1]))->b > j)
      return mid;

    if (((struct candidate*)(K[mid]))->b < j) low = mid + 1;
    else if (((struct candidate*)(K[mid]))->b > j) upper = mid - 1;
    else return -1;

    mid = (low + upper) / 2;
  }
  return -1;
}

static struct candidate * new_candidate (int i, int j, struct candidate* prev)
{
  struct candidate *c = xzalloc(sizeof(struct candidate));

  c->a = i;
  c->b = j;
  c->prev = prev;
  return c;
}


static void free_candidates(struct candidate *c)
{
  struct candidate *t = c;
  
  while ((t = c)) {
    c = c->next;
    free(t);
  }
}
/*
 * 1. Search K[r: k] for an element K[s] such that K[s]-> b < j and K[s + 1]->b > j
 * 2. if found do
 *  2.a. If K[s + 1]->b > j do K[r] = c; r = s+1 and c = candidate(i, j, K[s]) //we have a candidate
 *  2.b. if s = k (fence reached move it further) do K[k + 2] = K[k + 1], k++
 * 3. if E[p].last true break i.e we have reached at the end of an equiv class
 *    else p = p + 1 //keep traversing the equiv class.
 * 4. K[r] = c //Save the sucessfully filled k-candidate.
 */
static void  do_merge(struct candidate **K, int *k, int i,
    struct v_vector *E, int p)
{
  int r = 0, s, j;
  struct candidate *pr = 0, *c = K[0];

  while (1) {
    j = E[p].serial;
    s = search(K, r, *k, j);
    if (s >= 0 && (((struct candidate*)(K[s]))->b < j &&
          ((struct candidate*)(K[s + 1]))->b > j)) {

      if (((struct candidate*)(K[s + 1]))->b > j) {
        pr = K[s];
        if (r && K[r]) c->next = K[r];
        K[r] = c;
        r = s + 1;
        c = new_candidate(i , j, pr);
      }
      if (s == *k) {
        K[*k + 2] = K[*k + 1];
        *k = *k + 1;
        break;
      }
    }
    if (E[p].last) break;
    else p = p + 1;
  }
  K[r] = c;
}

static FILE* read_stdin()
{
  char tmp_name[] = "/tmp/diffXXXXXX";
  int rd, wr, tmpfd = mkstemp(tmp_name);

  if (tmpfd == -1) perror_exit("mkstemp");
  unlink(tmp_name);

  while (1) {
    rd = xread(STDIN_FILENO, toybuf, sizeof(toybuf));

    if (!rd) break;
    if (rd < 0) perror_exit("read error");
    wr = writeall(tmpfd, toybuf, rd);
    if (wr < 0) perror_exit("write");
  }
  return fdopen(tmpfd, "r");
}

static int read_tok(FILE *fp, off_t *off, int tok)
{
  int t = 0, is_space;

  tok |= empty;
  while (!(tok & eol)) {

    t = fgetc(fp);
    if (off && t != EOF) *off += 1;
    is_space = isspace(t) || (t == EOF);
    tok |= (t & (eof + eol)); //set tok eof+eol when t is eof

    if (t == '\n') tok |= eol;
    if (toys.optflags & FLAG_i)
      if (t >= 'A' && t <= 'Z') t = tolower(t);

    if (toys.optflags & FLAG_w && is_space) continue;

    if (toys.optflags & FLAG_b) {
      if (tok & space) {
        if (is_space) continue;
        tok &= ~space;
      } else if (is_space) t = space + ' ';
    }
    tok &= ~(empty + 0xff);  //remove empty and char too.
    tok |= t; //add most recent char
    break;
  }

  return tok;
}

int bcomp(const void *a, const void *b) 
{
  struct v_vector *l = (struct v_vector*)a,
                  *r = (struct v_vector*)b;
  int ret = l->hash - r->hash;

  if (!ret) {
    if ((r -1)->last) return 0;
    else return -1;
  }
  return ret;
}
/*  file[0] corresponds file 1 and file[1] correspond file 2.
 * 1. calc hashes for both the files and store them in vector(v[0], v[1])
 * 2. sort file[1] with hash as primary and serial as sec. key
 * 3. Form the equivalance class of file[1] stored in e vector. It lists all the equivalence
 *    classes of lines in file[1], with e.last = true on the last element of each class.
 *    The elements are ordered by serial within classes.
 * 4. Form the p vector stored in  p_vector. p_vector[i], if non-zero, now points in e vector
 *    to the begining of the equiv class of lines in file[1] equivalent to line
 *    i in file[0].
 * 5. Form the k-candidates as discribed in do_merge.
 * 6. Create a vector J[i] = j, such that i'th line in file[0] is j'th line of
 *    file[1], i.e J comprises LCS
 */
static int * create_j_vector()
{
  int tok, i, j, size = 100, k;
  off_t off;
  long hash;
  int *p_vector, *J;
  struct v_vector *v[2], *e;
  struct candidate **kcand, *pr;

  for (i = 0; i < 2; i++) {
    tok = off = 0;
    hash = 5831;
    v[i] = xzalloc(size * sizeof(struct v_vector));
    TT.offset[i] = xzalloc(size * sizeof(int));
    file[i].len = 0;
    fseek(file[i].fp, 0, SEEK_SET);

    while (1) {
      tok  = read_tok(file[i].fp, &off, tok);
      if (!(tok & empty)) {
        hash = ((hash << 5) + hash) + (tok & 0xff);
        continue;
      }

      if (size == ++file[i].len) {
        size = size * 11 / 10;
        v[i] = xrealloc(v[i], size*sizeof(struct v_vector));
        TT.offset[i] = xrealloc(TT.offset[i], size*sizeof(int));
      }

      v[i][file[i].len].hash = hash & INT_MAX;
      TT.offset[i][file[i].len] = off;
      if ((tok & eof)) {
        TT.offset[i][file[i].len] = ++off;
        break;
      }
      hash = 5831;  //next line
      tok = 0;
    }
    if (TT.offset[i][file[i].len] - TT.offset[i][file[i].len - 1] == 1)
      file[i].len--;
  }

  for (i = 0; i <= file[1].len; i++) v[1][i].serial = i;
  qsort(v[1] + 1, file[1].len, sizeof(struct v_vector), comp);

  e = v[1];
  e[0].serial = 0;
  e[0].last = 1;
  for ( i = 1; i <= file[1].len; i++) {
    if ((i == file[1].len) || (v[1][i].hash != v[1][i+1].hash)) e[i].last = 1;
    else e[i].last = 0;
  }

  p_vector = xzalloc((file[0].len + 2) * sizeof(int));
  for (i = 1; i <= file[0].len; i++) {
    void *r = bsearch(&v[0][i], (e + 1), file[1].len, sizeof(e[0]), bcomp);
    if (r) p_vector[i] = (struct v_vector*)r - e;
  }

  for (i = 1; i <= file[0].len; i++)
    e[i].p = p_vector[i];
  free(p_vector);

  size = 100;
  kcand = xzalloc(size * sizeof(struct candidate*));

  kcand[0] = new_candidate(0 , 0, NULL);
  kcand[1] = new_candidate(file[0].len+1, file[1].len+1, NULL); //the fence

  k = 0;  //last successfully filled k candidate.
  for (i = 1; i <= file[0].len; i++) {

    if (!e[i].p) continue;
    if ((size - 2) == k) {
      size = size * 11 / 10;
      kcand = xrealloc(kcand, (size * sizeof(struct candidate*)));
    }
    do_merge(kcand, &k, i, e, e[i].p);
  }
  free(v[0]); //no need for v_vector now.
  free(v[1]);

  J = xzalloc((file[0].len + 2) * sizeof(int));

  for (pr = kcand[k]; pr; pr = pr->prev)
    J[pr->a] = pr->b;
  J[file[0].len + 1] = file[1].len+1; //mark boundary

  for (i = k + 1; i >= 0; i--) free_candidates(kcand[i]);
  free(kcand);

  for (i = 1; i <= file[0].len; i++) { // jackpot?
    if (!J[i]) continue;

    fseek(file[0].fp, TT.offset[0][i - 1], SEEK_SET);
    fseek(file[1].fp, TT.offset[1][J[i] - 1], SEEK_SET);

    for (j = J[i]; i <= file[0].len && J[i] == j; i++, j++) {
      int tok0 = 0, tok1 = 0;

      do {
        tok0 = read_tok(file[0].fp, NULL, tok0);
        tok1 = read_tok(file[1].fp, NULL, tok1);
        if (((tok0 ^ tok1) & empty) || ((tok0 & 0xff) != (tok1 & 0xff)))
          J[i] = 0;
      } while (!(tok0 & tok1 & empty));
    }
  }
  return J;
}

static int *diff(char **files)
{
  size_t i ,j;
  int s, t;
  char *bufi, *bufj;

  TT.is_binary = 0; //loop calls to diff
  TT.status = SAME;

  for (i = 0; i < 2; i++) {
    if (IS_STDIN(files[i])) file[i].fp = read_stdin();
    else file[i].fp = fopen(files[i], "r");

    if (!file[i].fp){
      perror_msg("%s",files[i]);
      TT.status = 2;
      return NULL; //return SAME
    }
  }

  s = sizeof(toybuf)/2;
  bufi = toybuf;
  bufj = (toybuf + s);

  fseek(file[0].fp, 0, SEEK_SET);
  fseek(file[1].fp, 0, SEEK_SET);

  if (toys.optflags & FLAG_a) return create_j_vector();

  while (1) {
    i = fread(bufi, 1, s, file[0].fp);
    j = fread(bufj, 1, s, file[1].fp);

    if (i != j) TT.status = DIFFER;

    for (t = 0; t < i && !TT.is_binary; t++)
      if (!bufi[t]) TT.is_binary = 1;
    for (t = 0; t < j && !TT.is_binary; t++)
      if (!bufj[t]) TT.is_binary = 1;

    i = MIN(i, j);
    for (t = 0; t < i; t++)
      if (bufi[t] != bufj[t]) TT.status = DIFFER;

    if (!i || !j) break;
  }
  if (TT.is_binary || (TT.status == SAME)) return NULL;
  return create_j_vector();
}

static void print_diff(int a, int b, char c, int *off_set, FILE *fp)
{
  int i, j, cc, cl;

  for (i = a; i <= b; i++) {
    fseek(fp, off_set[i - 1], SEEK_SET);
    putchar(c);
    if (toys.optflags & FLAG_T) putchar('\t');
    for (j = 0, cl = 0; j <  (off_set[i] - off_set[i - 1]); j++) {
      cc = fgetc(fp);
      if (cc == EOF) {
        printf("\n\\ No newline at end of file\n");
        return;
      }
      if ((cc == '\t') && (toys.optflags & FLAG_t))
        do putchar(' '); while (++cl & 7);
      else {
        putchar(cc); //xputc has calls to fflush, it hurts performance badly.
        cl++;
      }
    }
  }
}

static char *concat_file_path(char *path, char *default_path)
{
  char *final_path;

  if ('/' == path[strlen(path) - 1]) {
    while (*default_path == '/') ++default_path;
    final_path = xmprintf("%s%s", path, default_path);
  }
  else if (*default_path != '/')
    final_path = xmprintf("%s/%s", path, default_path);
  else final_path = xmprintf("%s%s", path, default_path);
  return final_path;
}

static int skip(struct dirtree *node)
{
  int len = strlen(toys.optargs[TT.dir_num]), ret = 0;
  char *tmp = NULL, *ptr, *f_path = dirtree_path(node, NULL);
  struct stat st;

  ptr = f_path;
  ptr += len;
  if (ptr[0]) {
    tmp = concat_file_path(toys.optargs[1 - TT.dir_num], ptr);
    if (tmp && !stat(tmp, &st)) ret = 0; //it is there on other side
    else ret = 1; //not present on other side.
  }
  free(f_path);
  if (tmp) free(tmp);
  return ret; //add otherwise
}

static void add_to_list(struct dirtree *node)
{
  char *full_path;

  dir[TT.dir_num].list = xrealloc(dir[TT.dir_num].list,
      (TT.size + 1)*sizeof(char*));
  TT.size++;
  full_path = dirtree_path(node, NULL);
  dir[TT.dir_num].list[TT.size - 1] = full_path;
}

static int list_dir (struct dirtree *node)
{
  int ret = 0;

  if (!dirtree_notdotdot(node)) return 0;

  if (S_ISDIR(node->st.st_mode) && !node->parent) { //add root dirs.
    add_to_list(node);
    return (DIRTREE_RECURSE|DIRTREE_SYMFOLLOW);
  }

  if (S_ISDIR(node->st.st_mode) && (toys.optflags & FLAG_r)) {
    if (!(toys.optflags & FLAG_N)) ret = skip(node);
    if (!ret) return (DIRTREE_RECURSE|DIRTREE_SYMFOLLOW);
    else {
      add_to_list(node); //only at one side.
      return 0;
    }
  } else {
    add_to_list(node);
    return S_ISDIR(node->st.st_mode) ? 0 : (DIRTREE_RECURSE|DIRTREE_SYMFOLLOW);
  }
}

static int cmp(const void *p1, const void *p2)
{
   return strcmp(* (char * const *)p1, * (char * const *)p2);
}

static void do_diff(char **files)
{

  long i = 1, size = 1, x = 0, change = 0, ignore_white,
   start1, end1, start2, end2;
  struct diff *d;
  struct arg_list *llist = TT.L_list;
  int *J;
  
  TT.offset[0] = TT.offset[1] = NULL;
  J = diff(files);

  if (!J) return; //No need to compare, have to status only

  d = xzalloc(size *sizeof(struct diff));
  do {
    ignore_white = 0;
    for (d[x].a = i; d[x].a <= file[0].len; d[x].a++) {
      if (J[d[x].a] != (J[d[x].a - 1] + 1)) break;
      else continue;
    }
    d[x].c = (J[d[x].a - 1] + 1);

    for (d[x].b = (d[x].a - 1); d[x].b <= file[0].len; d[x].b++) {
      if (J[d[x].b + 1]) break;
      else continue;
    }
    d[x].d = (J[d[x].b + 1] - 1);

    if ((toys.optflags & FLAG_B)) {
      if (d[x].a <= d[x].b) {
        if ((TT.offset[0][d[x].b] - TT.offset[0][d[x].a - 1])
            == (d[x].b - d[x].a + 1))
          ignore_white = 1;
      } else if (d[x].c <= d[x].d){
        if ((TT.offset[1][d[x].d] - TT.offset[1][d[x].c - 1])
            == (d[x].d - d[x].c + 1))
          ignore_white = 1;
      }
    }

    if ((d[x].a <= d[x].b || d[x].c <= d[x].d) && !ignore_white)
      change = 1; //is we have diff ?

    if (!ignore_white) d = xrealloc(d, (x + 2) *sizeof(struct diff));
    i = d[x].b + 1;
    if (i > file[0].len) break;
    J[d[x].b] = d[x].d;
    if (!ignore_white) x++;
  } while (i <= file[0].len);

  i = x+1;
  TT.status = change; //update status, may change bcoz of -w etc.

  if (!(toys.optflags & FLAG_q) && change) {  //start of !FLAG_q

      xprintf("--- %s\n", (toys.optflags & FLAG_L) ? llist->arg : files[0]);
      if (((toys.optflags & FLAG_L) && !llist->next) || !(toys.optflags & FLAG_L))
        xprintf("+++ %s\n", files[1]);
      else {
        while (llist->next) llist = llist->next;
        xprintf("+++ %s\n", llist->arg);
      }

    struct diff *t, *ptr1 = d, *ptr2 = d;
    while (i) {
      long a,b;

      if (TT.ct > file[0].len) TT.ct = file[0].len; //trim context to file len.
      if (ptr1->b < ptr1->a && ptr1->d < ptr1->c) {
        i--;
        continue;
      }
      //Handle the context stuff
      a =  ptr1->a;
      b =  ptr1->b;

      b  = MIN(file[0].len, b);
      if (i == x + 1) ptr1->suff = MAX(1,a - TT.ct);
      else {
        if ((ptr1 - 1)->prev >= (ptr1->a - TT.ct))
          ptr1->suff = (ptr1 - 1)->prev + 1;
        else ptr1->suff =  ptr1->a - TT.ct;
      }
calc_ct:
      if (i > 1) {
        if ((ptr2->b + TT.ct) >= (ptr2  + 1)->a) {
          ptr2++;
          i--;
          goto calc_ct;
        } else ptr2->prev = ptr2->b + TT.ct;
      } else ptr2->prev = ptr2->b;
      start1 = (ptr2->prev - ptr1->suff + 1);
      end1 = (start1 == 1) ? -1 : start1;
      start2 = MAX(1, ptr1->c - (ptr1->a - ptr1->suff));
      end2 = ptr2->prev - ptr2->b + ptr2->d;

      printf("@@ -%ld", start1 ? ptr1->suff: (ptr1->suff -1));
      if (end1 != -1) printf(",%ld ", ptr2->prev-ptr1->suff + 1);
      else putchar(' ');

      printf("+%ld", (end2 - start2 + 1) ? start2: (start2 -1));
      if ((end2 - start2 +1) != 1) printf(",%ld ", (end2 - start2 +1));
      else putchar(' ');
      printf("@@\n");

      for (t = ptr1; t <= ptr2; t++) {
        if (t== ptr1) print_diff(t->suff, t->a-1, ' ', TT.offset[0], file[0].fp);
        print_diff(t->a, t->b, '-', TT.offset[0], file[0].fp);
        print_diff(t->c, t->d, '+', TT.offset[1], file[1].fp);
        if (t == ptr2)
          print_diff(t->b+1, (t)->prev, ' ', TT.offset[0], file[0].fp);
        else print_diff(t->b+1, (t+1)->a-1, ' ', TT.offset[0], file[0].fp);
      }
      ptr2++;
      ptr1 = ptr2;
      i--;
    } //end of while
  } //End of !FLAG_q
  free(d);
  free(J);
  free(TT.offset[0]);
  free(TT.offset[1]);
}

static void show_status(char **files)
{
  switch (TT.status) {
    case SAME:
      if (toys.optflags & FLAG_s)
        printf("Files %s and %s are identical\n",files[0], files[1]);
      break;
    case DIFFER:
      if ((toys.optflags & FLAG_q) || TT.is_binary)
        printf("Files %s and %s differ\n",files[0], files[1]);
      break;
  }
}

static void create_empty_entry(int l , int r, int j)
{
  struct stat st[2];
  char *f[2], *path[2];
  int i;

  if (j > 0 && (toys.optflags & FLAG_N)) {
    path[0] = concat_file_path(dir[0].list[0], dir[1].list[r] + TT.len[1]);
    f[0] = "/dev/null";
    path[1] = f[1] = dir[1].list[r];
    stat(f[1], &st[0]);
    st[1] = st[0];
  }
  else if (j < 0 && (toys.optflags & FLAG_N)) {
    path[1] = concat_file_path(dir[1].list[0], dir[0].list[l] + TT.len[0]);
    f[1] = "/dev/null";
    path[0] = f[0] = dir[0].list[l];
    stat(f[0], &st[0]);
    st[1] = st[0];
  }

  if (!j) {
    for (i = 0; i < 2; i++) {
      path[i] = f[i] = dir[i].list[!i ? l: r];
      stat(f[i], &st[i]);
    }
  }

  if (S_ISDIR(st[0].st_mode) && S_ISDIR(st[1].st_mode))
    printf("Common subdirectories: %s and %s\n", path[0], path[1]);
  else if (!S_ISREG(st[0].st_mode) && !S_ISDIR(st[0].st_mode))
    printf("File %s is not a regular file or directory "
        "and was skipped\n", path[0]);
  else if (!S_ISREG(st[1].st_mode) && !S_ISDIR(st[1].st_mode))
    printf("File %s is not a regular file or directory "
        "and was skipped\n", path[1]);
  else if (S_ISDIR(st[0].st_mode) != S_ISDIR(st[1].st_mode)) {
    if (S_ISDIR(st[0].st_mode))
      printf("File %s is a %s while file %s is a"
          " %s\n", path[0], "directory", path[1], "regular file");
    else
      printf("File %s is a %s while file %s is a"
          " %s\n", path[0], "regular file", path[1], "directory");
  } else {
    do_diff(f);
    show_status(path);
    if (file[0].fp) fclose(file[0].fp);
    if (file[1].fp) fclose(file[1].fp);
  }

  if ((toys.optflags & FLAG_N) && j) {
    if (j > 0) free(path[0]);
    else free(path[1]);
  }
}

static void diff_dir(int *start)
{
  int l, r, j = 0;

  l = start[0]; //left side file start
  r = start[1]; //right side file start
  while (l < dir[0].nr_elm && r < dir[1].nr_elm) {
    if ((j = strcmp ((dir[0].list[l] + TT.len[0]),
            (dir[1].list[r] + TT.len[1]))) && !(toys.optflags & FLAG_N)) {
      if (j > 0) {
        printf ("Only in %s: %s\n", dir[1].list[0], dir[1].list[r] + TT.len[1]);
        free(dir[1].list[r]);
        r++;
      } else {
        printf ("Only in %s: %s\n", dir[0].list[0], dir[0].list[l] + TT.len[0]);
        free(dir[0].list[l]);
        l++;
      }
      TT.status = DIFFER;
    } else {
      create_empty_entry(l, r, j); //create non empty dirs/files if -N.
      if (j > 0) {
        free(dir[1].list[r]);
        r++;
      } else if (j < 0) {
        free(dir[0].list[l]);
        l++;
      } else {
        free(dir[1].list[r]);
        free(dir[0].list[l]);
        l++;
        r++;
      }
    }
  }

  if (l == dir[0].nr_elm) {
    while (r < dir[1].nr_elm) {
      if (!(toys.optflags & FLAG_N)) {
        printf ("Only in %s: %s\n", dir[1].list[0], dir[1].list[r] + TT.len[1]);
        TT.status = DIFFER;
      } else create_empty_entry(l, r, 1);
      free(dir[1].list[r]);
      r++;
    }
  } else if (r == dir[1].nr_elm) {
    while (l < dir[0].nr_elm) {
      if (!(toys.optflags & FLAG_N)) {
        printf ("Only in %s: %s\n", dir[0].list[0], dir[0].list[l] + TT.len[0]);
        TT.status = DIFFER;
      } else create_empty_entry(l, r, -1);
      free(dir[0].list[l]);
      l++;
    }
  }
  free(dir[0].list[0]); //we are done, free root nodes too
  free(dir[1].list[0]);
}

void diff_main(void)
{
  struct stat st[2];
  int j = 0, k = 1, start[2] = {1, 1};
  char *files[2];

  for (j = 0; j < 2; j++) {
    files[j] = toys.optargs[j];
    if (IS_STDIN(files[j])) {
      if (fstat(0, &st[j]) == -1)
        perror_exit("can fstat %s", files[j]);
    } else {
      if (stat(files[j], &st[j]) == -1)
        perror_exit("can't stat %s", files[j]);
    }
  }

  if (IS_STDIN(files[0]) && IS_STDIN(files[1])) { //compat :(
    show_status(files);  //check ASAP
    return;
  }

  if ((IS_STDIN(files[0]) || IS_STDIN(files[1]))
      && (S_ISDIR(st[0].st_mode) || S_ISDIR(st[1].st_mode)))
    error_exit("can't compare stdin to directory");

  if ((st[0].st_ino == st[1].st_ino) //physicaly same device
      &&(st[0].st_dev == st[1].st_dev)) {
    show_status(files);
    return ;
  }

  if (S_ISDIR(st[0].st_mode) && S_ISDIR(st[1].st_mode)) {
    for (j = 0; j < 2; j++) {
      memset(&dir[j], 0, sizeof(struct dir_t));
      dirtree_flagread(files[j], DIRTREE_SYMFOLLOW, list_dir);
      dir[j].nr_elm = TT.size; //size updated in list_dir
      qsort(&(dir[j].list[1]), (TT.size - 1), sizeof(char*), cmp);

      TT.len[j] = strlen(dir[j].list[0]); //calc root node len
      TT.len[j] += (dir[j].list[0][TT.len[j] -1] != '/');

      if (toys.optflags & FLAG_S) {
        while (k < TT.size && strcmp(dir[j].list[k] +
              TT.len[j], TT.start) < 0) {
          start[j] += 1;
          k++;
        }
      }
      TT.dir_num++;
      TT.size = 0;
      k = 1;
    }
    diff_dir(start);
    free(dir[0].list); //free array
    free(dir[1].list);
  } else {
    if (S_ISDIR(st[0].st_mode) || S_ISDIR(st[1].st_mode)) {
      int d = S_ISDIR(st[0].st_mode);
      char *slash = strrchr(files[d], '/');

      files[1 - d] = concat_file_path(files[1 - d], slash ? slash + 1 : files[d]);
      if ((stat(files[1 - d], &st[1 - d])) == -1)
        perror_exit("%s", files[1 - d]);
    }
    do_diff(files);
    show_status(files);
    if (file[0].fp) fclose(file[0].fp);
    if (file[1].fp) fclose(file[1].fp);
  }
  toys.exitval = TT.status; //exit status will be the status
}
