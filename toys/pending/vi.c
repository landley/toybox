/* vi.c - You can't spell "evil" without "vi".
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 * Copyright 2019 Jarno Mäkipää <jmakip87@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/vi.html

USE_VI(NEWTOY(vi, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config VI
  bool "vi"
  default n
  help
    usage: vi FILE
    Visual text editor. Predates the existence of standardized cursor keys,
    so the controls are weird and historical.
*/

#define FOR_vi
#include "toys.h"

GLOBALS(
    int cur_col;
    int cur_row;
    int scr_row;
    int drawn_row;
    int drawn_col;
    unsigned screen_height;
    unsigned screen_width;
    int vi_mode;
    int count0;
    int count1;
    int vi_mov_flag;
    int modified;
    char vi_reg;
    char *last_search;
    int tabstop;
    int list;
)

struct str_line {
  int alloc;
  int len;
  char *data;
};
//yank buffer
struct yank_buf {
  char reg;
  int alloc;
  char* data;
};


//lib dllist uses next and prev kinda opposite what im used to so I just
//renamed both ends to up and down
struct linelist {
  struct linelist *up;//next
  struct linelist *down;//prev
  struct str_line *line;
};

static void draw_page();

//utf8 support
static int utf8_lnw(int* width, char* str, int bytes);
static int utf8_dec(char key, char *utf8_scratch, int *sta_p);
static int utf8_width(char *str, int bytes);
static char* utf8_last(char* str, int size);


static int cur_left(int count0, int count1, char* unused);
static int cur_right(int count0, int count1, char* unused);
static int cur_up(int count0, int count1, char* unused);
static int cur_down(int count0, int count1, char* unused);
static void check_cursor_bounds();
static void adjust_screen_buffer();
static int search_str(char *s);

static int vi_yank(char reg, struct linelist *row, int col, int flags);
static int vi_delete(char reg, struct linelist *row, int col, int flags);

//inserted line not yet pushed to buffer
struct str_line *il;
struct linelist *text; //file loaded into buffer
struct linelist *screen;//current screen coord 0 row
struct linelist *c_r;//cursor position row

struct yank_buf yank; //single yank

// TT.vi_mov_flag is used for special cases when certain move
// acts differently depending is there DELETE/YANK or NOP
// Also commands such as G does not default to count0=1
// 0x1 = Command needs argument (f,F,r...)
// 0x2 = Move 1 right on yank/delete/insert (e, $...)
// 0x4 = yank/delete last line fully
// 0x10000000 = redraw after cursor needed
// 0x20000000 = full redraw needed
// 0x40000000 = count0 not given
// 0x80000000 = move was reverse

void dlist_insert_nomalloc(struct double_list **list, struct double_list *new)
{
  if (*list) {
    new->next = *list;
    new->prev = (*list)->prev;
    if ((*list)->prev) (*list)->prev->next = new;
    (*list)->prev = new;
  } else *list = new->next = new->prev = new;
}


// Add an entry to the end of a doubly linked list
struct double_list *dlist_insert(struct double_list **list, char *data)
{
  struct double_list *new = xmalloc(sizeof(struct double_list));
  new->data = data;
  dlist_insert_nomalloc(list, new);

  return new;
}

void linelist_free(void *node)
{
  struct linelist *lst = (struct linelist *)node;
  free(lst->line->data), free(lst->line), free(lst);
}

void linelist_unload()
{
  void* list = 0;
  for (;text->down; text = text->down);
  list = (void*)text;
  text = screen = c_r = 0;
  llist_traverse(list, linelist_free);
}

void write_file(char *filename)
{
  struct linelist *lst = text;
  FILE *fp = 0;
  if (!filename) filename = (char*)*toys.optargs;
  if (!(fp = fopen(filename, "w")) ) return;

  for (;lst; lst = lst->down)
    fprintf(fp, "%s\n", lst->line->data);

  fclose(fp);
}

int linelist_load(char *filename)
{
  struct linelist *lst = c_r;//cursor position or 0
  FILE *fp = 0;
  if (!filename)
    filename = (char*)*toys.optargs;

  fp = fopen(filename, "r");
  if (!fp) {
    char *line = xzalloc(80);
    ssize_t alc = 80;
    lst = (struct linelist*)dlist_add((struct double_list**)&lst,
        xzalloc(sizeof(struct str_line)));
    lst->line->alloc = alc;
    lst->line->len = 0;
    lst->line->data = line;
    text = lst;
    dlist_terminate(text->up);
    return 1;
  }

  for (;;) {
    char *line = xzalloc(80);
    ssize_t alc = 80;
    ssize_t len;
    if ((len = getline(&line, (void *)&alc, fp)) == -1) {
      if (errno == EINVAL || errno == ENOMEM) {
        printf("error %d\n", errno);
      }
      free(line);
      break;
    }
    lst = (struct linelist*)dlist_add((struct double_list**)&lst,
        xzalloc(sizeof(struct str_line)));
    lst->line->alloc = alc;
    lst->line->len = len;
    lst->line->data = line;

    if (lst->line->data[len-1] == '\n') {
      lst->line->data[len-1] = 0;
      lst->line->len--;
    }
    if (text == 0) text = lst;
  }

  if (text) dlist_terminate(text->up);

  fclose(fp);
  return 1;

}

int vi_yy(char reg, int count0, int count1)
{
  struct linelist *pos = c_r;
  int col = TT.cur_col;
  TT.cur_col = 0;
  TT.vi_mov_flag |= 0x4;

  if (count0>1) cur_down(count0-1, 1, 0);

  vi_yank(reg, pos, 0, 0);

  TT.cur_col = col, c_r = pos;
  return 1;
}

int vi_dd(char reg, int count0, int count1)
{
  struct linelist *pos = c_r;
  TT.cur_col = 0;
  TT.vi_mov_flag |= 0x4;
  if (count0>1) cur_down(count0-1, 1, 0);

  vi_delete(reg, pos, 0, 0);
  check_cursor_bounds();
  return 1;
}

static int vi_x(char reg, int count0, int count1)
{
  char *last = 0, *cpos = 0, *start = 0;
  int len = 0;
  struct linelist *pos = c_r;
  int col = TT.cur_col;
  if (!c_r) return 0;

  start = c_r->line->data;
  len = c_r->line->len;

  last = utf8_last(start, len);
  cpos = start+TT.cur_col;
  if (cpos == last) {
    cur_left(count0-1, 1, 0);
    col = strlen(start);
  }
  else {
    cur_right(count0-1, 1, 0);
    cpos = start+TT.cur_col;
    if (cpos == last) TT.vi_mov_flag |= 2;
    else cur_right(1, 1, 0);
  }

  vi_delete(reg, pos, col, 0);
  check_cursor_bounds();
  return 1;
}

//move commands does not behave correct way yet.
int vi_movw(int count0, int count1, char* unused)
{
  int count = count0*count1;
  const char *empties = " \t\n\r";
  const char *specials = ",.=-+*/(){}<>[]";
//  char *current = 0;
  if (!c_r)
    return 0;
  if (TT.cur_col == c_r->line->len-1 || !c_r->line->len)
    goto next_line;
  if (strchr(empties, c_r->line->data[TT.cur_col]))
    goto find_non_empty;
  if (strchr(specials, c_r->line->data[TT.cur_col])) {
    for (;strchr(specials, c_r->line->data[TT.cur_col]); ) {
      TT.cur_col++;
      if (TT.cur_col == c_r->line->len-1)
        goto next_line;
    }
  } else for (;!strchr(specials, c_r->line->data[TT.cur_col]) &&
      !strchr(empties, c_r->line->data[TT.cur_col]);) {
      TT.cur_col++;
      if (TT.cur_col == c_r->line->len-1)
        goto next_line;
  }

  for (;strchr(empties, c_r->line->data[TT.cur_col]); ) {
    TT.cur_col++;
find_non_empty:
    if (TT.cur_col == c_r->line->len-1) {
next_line:
      //we could call j and g0
      if (!c_r->down) return 0;
      c_r = c_r->down;
      TT.cur_col = 0;
      if (!c_r->line->len) break;
    }
  }
  count--;
  if (count>0)
    return vi_movw(count, 1, 0);

  check_cursor_bounds();
  return 1;
}

static int vi_movb(int count0, int count1, char* unused)
{
  int count = count0*count1;
  if (!c_r)
    return 0;
  if (!TT.cur_col) {
      if (!c_r->up) return 0;
      c_r = c_r->up;
      TT.cur_col = (c_r->line->len) ? c_r->line->len-1 : 0;
      goto exit_function;
  }
  if (TT.cur_col)
      TT.cur_col--;
  while (c_r->line->data[TT.cur_col] <= ' ') {
    if (TT.cur_col) TT.cur_col--;
    else goto exit_function;
  }
  while (c_r->line->data[TT.cur_col] > ' ') {
    if (TT.cur_col)TT.cur_col--;
    else goto exit_function;
  }
  TT.cur_col++;
exit_function:
  count--;
  if (count>1)
    return vi_movb(count, 1, 0);
  TT.vi_mov_flag |= 0x80000000;
  check_cursor_bounds();
  return 1;
}

static int vi_move(int count0, int count1, char *unused)
{
  int count = count0*count1;
  if (!c_r)
    return 0;
  if (TT.cur_col < c_r->line->len)
    TT.cur_col++;
  if (c_r->line->data[TT.cur_col] <= ' ' || count > 1)
    vi_movw(count, 1, 0); //find next word;
  while (c_r->line->data[TT.cur_col] > ' ')
    TT.cur_col++;
  if (TT.cur_col) TT.cur_col--;

  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  return 1;
}


static void i_insert(char* str, int len)
{
  char *t = xzalloc(c_r->line->alloc);
  char *s = c_r->line->data;
  int sel = c_r->line->len-TT.cur_col;
  strncpy(t, &s[TT.cur_col], sel);
  t[sel+1] = 0;
  if (c_r->line->alloc < c_r->line->len+len+5) {
    c_r->line->data = xrealloc(c_r->line->data,
      (c_r->line->alloc+len)<<1);

    c_r->line->alloc = (c_r->line->alloc+len)<<1;
    memset(&c_r->line->data[c_r->line->len], 0,
        c_r->line->alloc-c_r->line->len);

    s = c_r->line->data;
  }
  strncpy(&s[TT.cur_col], str, len);
  strcpy(&s[TT.cur_col+len], t);
  TT.cur_col += len;
  if (TT.cur_col) TT.cur_col--;

  c_r->line->len += len;
  free(t);

  TT.vi_mov_flag |= 0x30000000;
}

//new line at split pos;
void i_split()
{
  int alloc = 0, len = 0, idx = 0;
  struct str_line *l = xmalloc(sizeof(struct str_line));
  alloc = c_r->line->alloc;

  if (TT.cur_col) len = c_r->line->len-TT.cur_col-1;
  else len = c_r->line->len;
  if (len < 0) len = 0;

  l->data = xzalloc(alloc);
  l->alloc = alloc;
  l->len = len;
  idx = c_r->line->len - len;

  strncpy(l->data, &c_r->line->data[idx], len);
  memset(&l->data[len], 0, alloc-len);

  c_r->line->len -= len;
  if (c_r->line->len <= 0) c_r->line->len = 0;

  len = c_r->line->len;

  memset(&c_r->line->data[len], 0, alloc-len);
  c_r = (struct linelist*)dlist_insert((struct double_list**)&c_r, (char*)l);
  c_r->line = l;
  TT.cur_col = 0;
}


static int vi_zero(int count0, int count1, char *unused)
{
  TT.cur_col = 0;
  TT.vi_mov_flag |= 0x80000000;
  return 1;
}

static int vi_eol(int count0, int count1, char *unused)
{
  int count = count0*count1;
  for (;count > 1 && c_r->down; count--)
    c_r = c_r->down;

  if (c_r && c_r->line->len)
    TT.cur_col = c_r->line->len-1;
  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  return 1;
}

//TODO check register where to push from
static int vi_push(char reg, int count0, int count1)
{
  char *start = yank.data, *end = yank.data+strlen(yank.data);
  struct linelist *cursor = c_r;
  int col = TT.cur_col;
  //insert into new lines
  if (*(end-1) == '\n') for (;start != end;) {
    TT.vi_mov_flag |= 0x10000000;
    char *next = strchr(start, '\n');
    TT.cur_col = (c_r->line->len) ? c_r->line->len-1: 0;
    i_split();
    if (next) {
      i_insert(start, next-start);
      start = next+1;
    } else start = end; //??
  }

  //insert into cursor
  else for (;start != end;) {
    char *next = strchr(start, '\n');
    if (next) {
      TT.vi_mov_flag |= 0x10000000;
      i_insert(start, next-start);
      i_split();
      start = next+1;
    } else {
      i_insert(start, strlen(start));
      start = end;
    }
  }
  //if row changes during push original cursor position is kept
  //vi inconsistancy
  if (c_r != cursor) c_r = cursor, TT.cur_col = col;

  return 1;
}

static int vi_find_c(int count0, int count1, char *symbol)
{
  int count = count0*count1;
  if (c_r && c_r->line->len) {
    while (count--) {
        char* pos = strstr(&c_r->line->data[TT.cur_col], symbol);
        if (pos) {
          TT.cur_col = pos-c_r->line->data;
          return 1;
        }
    }
  }
  return 0;
}

static int vi_find_cb(int count0, int count1, char *symbol)
{
  //do backward search
  return 1;
}

//if count is not spesified should go to last line
static int vi_go(int count0, int count1, char *symbol)
{
  int prev_row = TT.cur_row;
  c_r = text;

  if (TT.vi_mov_flag&0x40000000) for (;c_r && c_r->down; c_r = c_r->down);
  else for (;c_r && c_r->down && --count0; c_r = c_r->down);

  TT.cur_col = 0;
  check_cursor_bounds();  //adjusts cursor column
  if (prev_row>TT.cur_row) TT.vi_mov_flag |= 0x80000000;

  return 1;
}

//need to refactor when implementing yank buffers
static int vi_delete(char reg, struct linelist *row, int col, int flags)
{
  struct linelist *start = 0, *end = 0;
  int col_s = 0, col_e = 0, bytes = 0;

  vi_yank(reg, row, col, flags);

  if (TT.vi_mov_flag&0x80000000) {
    start = c_r, end = row;
    col_s = TT.cur_col, col_e = col;
  } else {
    start = row, end = c_r;
    col_s = col, col_e = TT.cur_col;
  }
  if (start == end) goto last_line_delete;
  if (!col_s) goto full_line_delete;

  memset(start->line->data+col_s, 0, start->line->len-col_s);
  row->line->len = col_s;
  col_s = 0;
  start = start->down;

full_line_delete:
  TT.vi_mov_flag |= 0x10000000;
  for (;start != end;) {
    struct linelist* lst = start;
    //struct linelist *lst = dlist_pop(&start);
    start = start->down;
    if (lst->down) lst->down->up = lst->up;
    if (lst->up) lst->up->down = lst->down;
    if (screen == lst) screen = lst->down ? lst->down : lst->up;
    if (text == lst) text = lst->down;
    free(lst->line->data);
    free(lst->line);
    free(lst);
  }
last_line_delete:
  TT.vi_mov_flag |= 0x10000000;
  if (TT.vi_mov_flag&2) col_e = start->line->len;
  if (TT.vi_mov_flag&4) {
    if (!end->down && !end->up)
      col_e = start->line->len;
    else {
      col_e = 0, col_s = 0;
      if (end->down) end->down->up = end->up;
      if (end->up) end->up->down = end->down;
      if (screen == end) screen = end->down ? end->down : end->up;
      //if (text == end) text = end->down;
      start = end->down ? end->down : end->up;
      free(end->line->data);
      free(end->line);
      free(end);

    }
  }
  if (col_s < col_e) {
    bytes = col_s + start->line->len - col_e;
    memmove(start->line->data+col_s, start->line->data+col_e,
        start->line->len-col_e);
    memset(start->line->data+bytes, 0, start->line->len-bytes);
    start->line->len = bytes;
  }
  c_r = start;
  TT.cur_col = col_s;
  return 1;
}

static int vi_D(char reg, int count0, int count1)
{
  int prev_col = TT.cur_col;
  struct linelist *pos = c_r;
  if (!count0) return 1;
  vi_eol(1, 1, 0);
  vi_delete(reg, pos, prev_col, 0);
  count0--;
  if (count0 && c_r->down) {
    c_r = c_r->down;
    vi_dd(reg, count0, 1);
  }
  check_cursor_bounds();
  return 1;
}

static int vi_join(char reg, int count0, int count1)
{
  while (count0--) {
    if (c_r && c_r->down) {
      int size = c_r->line->len+c_r->down->line->len;
      if (size > c_r->line->alloc) {
        if (size > c_r->down->line->alloc) {
          c_r->line->data = xrealloc(c_r->line->data,
            c_r->line->alloc*2+il->alloc*2);
          memmove(&c_r->line->data[c_r->line->len],
              c_r->down->line->data,c_r->down->line->len);
          c_r->line->len = size;
          c_r = c_r->down;
          c_r->line->alloc = c_r->line->alloc*2+2*il->alloc;
          vi_dd(0,1,1);
        } else {
          memmove(&c_r->down->line->data[c_r->line->len],
              c_r->down->line->data,c_r->down->line->len);
          memmove(c_r->down->line->data,c_r->line->data,
              c_r->line->len);
          c_r->down->line->len = size;
          vi_dd(0,1,1);
        }
      } else {
          memmove(&c_r->line->data[c_r->line->len],
              c_r->down->line->data,c_r->down->line->len);
          c_r->line->len = size;
          c_r = c_r->down;
          vi_dd(0,1,1);
      }
      c_r = c_r->up;

    }
  }
  return 1;
}

static int vi_find_next(char reg, int count0, int count1)
{
  if (TT.last_search) search_str(TT.last_search);
  return 1;
}

static int vi_change(char reg, struct linelist *row, int col, int flags)
{
  vi_delete(reg, row, col, flags);
  TT.vi_mode = 2;
  return 1;
}

//TODO search yank buffer by register
//now only supports default register
static int vi_yank(char reg, struct linelist *row, int col, int flags)
{
  struct linelist *start = 0, *end = 0;
  int col_s = 0, col_e = 0, bytes = 0;

  memset(yank.data, 0, yank.alloc);
  if (TT.vi_mov_flag&0x80000000) {
    start = c_r, end = row;
    col_s = TT.cur_col, col_e = col;
  } else {
    start = row, end = c_r;
    col_s = col, col_e = TT.cur_col;
  }
  if (start == end) goto last_line_yank;
  if (!col_s) goto full_line_yank;

  if (yank.alloc < start->line->alloc) {
    yank.data = xrealloc(yank.data, start->line->alloc*2);
    yank.alloc = start->line->alloc*2;
  }

  sprintf(yank.data, "%s\n", start->line->data+col_s);
  col_s = 0;
  start = start->down;

full_line_yank:
  for (;start != end;) {
    while (yank.alloc-1 < strlen(yank.data)+start->line->len)
      yank.data = xrealloc(yank.data, yank.alloc*2), yank.alloc *= 2;


    sprintf(yank.data+strlen(yank.data), "%s\n", start->line->data);
    start = start->down;
  }
last_line_yank:
  while (yank.alloc-1 < strlen(yank.data)+end->line->len)
    yank.data = xrealloc(yank.data, yank.alloc*2), yank.alloc *= 2;

  if (TT.vi_mov_flag & 0x4)
    sprintf(yank.data+strlen(yank.data), "%s\n", start->line->data);
  else {
    bytes = strlen(yank.data)+col_e-col_s;
    strncpy(yank.data+strlen(yank.data), end->line->data+col_s, col_e-col_s);
    yank.data[bytes] = 0;
  }
  return 1;
}

//NOTES
//vi-mode cmd syntax is
//("[REG])[COUNT0]CMD[COUNT1](MOV)
//where:
//-------------------------------------------------------------
//"[REG] is optional buffer where deleted/yanked text goes REG can be
//  atleast 0-9, a-z or default "
//[COUNT] is optional multiplier for cmd execution if there is 2 COUNT
//  operations they are multiplied together
//CMD is operation to be executed
//(MOV) is movement operation, some CMD does not require MOV and some
//  have special cases such as dd, yy, also movements can work without
//  CMD
//ex commands can be even more complicated than this....
//
struct vi_cmd_param {
  const char* cmd;
  unsigned flags;
  int (*vi_cmd)(char, struct linelist*, int, int);//REG,row,col,FLAGS
};
struct vi_mov_param {
  const char* mov;
  unsigned flags;
  int (*vi_mov)(int, int, char*);//COUNT0,COUNT1,params
};
//special cases without MOV and such
struct vi_special_param {
  const char *cmd;
  int (*vi_special)(char, int, int);//REG,COUNT0,COUNT1
};
struct vi_special_param vi_special[] =
{
  {"dd", &vi_dd},
  {"yy", &vi_yy},
  {"D", &vi_D},
  {"J", &vi_join},
  {"n", &vi_find_next},
  {"x", &vi_x},
  {"p", &vi_push}
};
//there is around ~47 vi moves
//some of them need extra params
//such as f and '
struct vi_mov_param vi_movs[] =
{
  {"0", 0, &vi_zero},
  {"b", 0, &vi_movb},
  {"e", 0, &vi_move},
  {"G", 0, &vi_go},
  {"h", 0, &cur_left},
  {"j", 0, &cur_down},
  {"k", 0, &cur_up},
  {"l", 0, &cur_right},
  {"w", 0, &vi_movw},
  {"$", 0, &vi_eol},
  {"f", 1, &vi_find_c},
  {"F", 1, &vi_find_cb},
};
//change and delete unfortunately behave different depending on move command,
//such as ce cw are same, but dw and de are not...
//also dw stops at w position and cw seem to stop at e pos+1...
//so after movement we need to possibly set up some flags before executing
//command, and command needs to adjust...
struct vi_cmd_param vi_cmds[] =
{
  {"c", 1, &vi_change},
  {"d", 1, &vi_delete},
  {"y", 1, &vi_yank},
};

int run_vi_cmd(char *cmd)
{
  int i = 0, val = 0;
  char *cmd_e;
  int (*vi_cmd)(char, struct linelist*, int, int) = 0;
  int (*vi_mov)(int, int, char*) = 0;

  TT.count0 = 0, TT.count1 = 0, TT.vi_mov_flag = 0;
  TT.vi_reg = '"';

  if (*cmd == '"') {
    cmd++;
    TT.vi_reg = *cmd; //TODO check validity
    cmd++;
  }
  errno = 0;
  val = strtol(cmd, &cmd_e, 10);
  if (errno || val == 0) val = 1, TT.vi_mov_flag |= 0x40000000;
  else cmd = cmd_e;
  TT.count0 = val;

  for (i = 0; i < ARRAY_LEN(vi_special); i++) {
    if (strstr(cmd, vi_special[i].cmd)) {
      return vi_special[i].vi_special(TT.vi_reg, TT.count0, TT.count1);
    }
  }

  for (i = 0; i < ARRAY_LEN(vi_cmds); i++) {
    if (!strncmp(cmd, vi_cmds[i].cmd, strlen(vi_cmds[i].cmd))) {
      vi_cmd = vi_cmds[i].vi_cmd;
      cmd += strlen(vi_cmds[i].cmd);
      break;
    }
  }
  errno = 0;
  val = strtol(cmd, &cmd_e, 10);
  if (errno || val == 0) val = 1;
  else cmd = cmd_e;
  TT.count1 = val;

  for (i = 0; i < ARRAY_LEN(vi_movs); i++) {
    if (!strncmp(cmd, vi_movs[i].mov, strlen(vi_movs[i].mov))) {
      vi_mov = vi_movs[i].vi_mov;
      TT.vi_mov_flag |= vi_movs[i].flags;
      cmd++;
      if (TT.vi_mov_flag&1 && !(*cmd)) return 0;
      break;
    }
  }
  if (vi_mov) {
    int prev_col = TT.cur_col;
    struct linelist *pos = c_r;
    if (vi_mov(TT.count0, TT.count1, cmd)) {
      if (vi_cmd) return (vi_cmd(TT.vi_reg, pos, prev_col, TT.vi_mov_flag));
      else return 1;
    } else return 0; //return some error
  }
  return 0;
}

static int search_str(char *s)
{
  struct linelist *lst = c_r;
  char *c = strstr(&c_r->line->data[TT.cur_col+1], s);

  if (TT.last_search != s) {
    free(TT.last_search);
    TT.last_search = xstrdup(s);
  }

  if (c) {
    TT.cur_col = c-c_r->line->data;
  } else for (; !c;) {
    lst = lst->down;
    if (!lst) return 1;
    c = strstr(lst->line->data, s);
  }
  c_r = lst;
  TT.cur_col = c-c_r->line->data;
  check_cursor_bounds();
  return 0;
}

int run_ex_cmd(char *cmd)
{
  if (cmd[0] == '/') {
    search_str(&cmd[1]);
  } else if (cmd[0] == '?') {
    // TODO: backwards search.
  } else if (cmd[0] == ':') {
    if (!strcmp(&cmd[1], "q") || !strcmp(&cmd[1], "q!")) {
      // TODO: if no !, check whether file modified.
      //exit_application;
      return -1;
    }
    else if (strstr(&cmd[1], "wq")) {
      write_file(0);
      return -1;
    }
    else if (strstr(&cmd[1], "w")) {
      write_file(0);
      return 1;
    }
    else if (strstr(&cmd[1], "set list")) {
      TT.list = 1;
      TT.vi_mov_flag |= 0x30000000;
      return 1;
    }
    else if (strstr(&cmd[1], "set nolist")) {
      TT.list = 0;
      TT.vi_mov_flag |= 0x30000000;
      return 1;
    }
  }
  return 0;

}

void vi_main(void)
{
  char keybuf[16] = {0};
  char vi_buf[16] = {0};
  char utf8_code[8] = {0};
  int utf8_dec_p = 0, vi_buf_pos = 0;

  il = xzalloc(sizeof(struct str_line));
  il->data = xzalloc(80);
  yank.data = xzalloc(128);

  il->alloc = 80, yank.alloc = 128;

  linelist_load(0);
  screen = c_r = text;

  TT.vi_mov_flag = 0x20000000;
  TT.vi_mode = 1, TT.tabstop = 8;
  TT.screen_width = 80, TT.screen_height = 24;

  terminal_size(&TT.screen_width, &TT.screen_height);
  TT.screen_height -= 2; //TODO this is hack fix visual alignment

  set_terminal(0, 1, 0, 0);
  //writes stdout into different xterm buffer so when we exit
  //we dont get scroll log full of junk
  tty_esc("?1049h");
  tty_esc("H");
  xflush(1);


  draw_page();
  for (;;) {
    int key = scan_key(keybuf, -1);

    if (key == -1) goto cleanup_vi;

    terminal_size(&TT.screen_width, &TT.screen_height);
    TT.screen_height -= 2; //TODO this is hack fix visual alignment

    // TODO: support cursor keys in ex mode too.
    if (TT.vi_mode && key>=256) {
      key -= 256;
      if (key==KEY_UP) cur_up(1, 1, 0);
      else if (key==KEY_DOWN) cur_down(1, 1, 0);
      else if (key==KEY_LEFT) cur_left(1, 1, 0);
      else if (key==KEY_RIGHT) cur_right(1, 1, 0);
      draw_page();
      continue;
    }

    if (TT.vi_mode == 1) { //NORMAL
      switch (key) {
        case '/':
        case '?':
        case ':':
          TT.vi_mode = 0;
          il->data[0]=key;
          il->len++;
          break;
        case 'A':
          vi_eol(1, 1, 0);
          // FALLTHROUGH
        case 'a':
          if (c_r && c_r->line->len) TT.cur_col++;
          // FALLTHROUGH
        case 'i':
          TT.vi_mode = 2;
          break;
        case 27:
          vi_buf[0] = 0;
          vi_buf_pos = 0;
          break;
        default:
          if (key > 0x20 && key < 0x7B) {
            vi_buf[vi_buf_pos] = key;//TODO handle input better
            vi_buf_pos++;
            if (run_vi_cmd(vi_buf)) {
              memset(vi_buf, 0, 16);
              vi_buf_pos = 0;
            }
            else if (vi_buf_pos == 16) {
              vi_buf_pos = 0;
              memset(vi_buf, 0, 16);
            }

          }

          break;
      }
    } else if (TT.vi_mode == 0) { //EX MODE
      switch (key) {
        case 0x7F:
        case 0x08:
          if (il->len > 1) {
            il->data[--il->len] = 0;
            break;
          }
          // FALLTHROUGH
        case 27:
          TT.vi_mode = 1;
          il->len = 0;
          memset(il->data, 0, il->alloc);
          break;
        case 0x0D:
          if (run_ex_cmd(il->data) == -1)
            goto cleanup_vi;
          TT.vi_mode = 1;
          il->len = 0;
          memset(il->data, 0, il->alloc);
          break;
        default: //add chars to ex command until ENTER
          if (key >= 0x20 && key < 0x7F) { //might be utf?
            if (il->len == il->alloc) {
              il->data = realloc(il->data, il->alloc*2);
              il->alloc *= 2;
            }
            il->data[il->len] = key;
            il->len++;
          }
          break;
      }
    } else if (TT.vi_mode == 2) {//INSERT MODE
      switch (key) {
        case 27:
          i_insert(il->data, il->len);
          TT.vi_mode = 1;
          il->len = 0;
          memset(il->data, 0, il->alloc);
          break;
        case 0x7F:
        case 0x08:
          if (il->len) {
            char *last = utf8_last(il->data, il->len);
            int shrink = strlen(last);
            memset(last, 0, shrink);
            il->len -= shrink;
          }
          break;
        case 0x0D:
          //insert newline
          //
          i_insert(il->data, il->len);
          il->len = 0;
          memset(il->data, 0, il->alloc);
          i_split();
          break;
        default:
          if ((key >= 0x20 || key == 0x09) &&
              utf8_dec(key, utf8_code, &utf8_dec_p)) {

            if (il->len+utf8_dec_p+1 >= il->alloc) {
              il->data = realloc(il->data, il->alloc*2);
              il->alloc *= 2;
            }
            strcpy(il->data+il->len, utf8_code);
            il->len += utf8_dec_p;
            utf8_dec_p = 0;
            *utf8_code = 0;

          }
          break;
      }
    }

    draw_page();

  }
cleanup_vi:
  linelist_unload();
  free(il->data), free(il), free(yank.data);
  tty_reset();
  tty_esc("?1049l");
}

int vi_crunch(FILE* out, int cols, int wc)
{
  int ret = 0;
  if (wc < 32 && TT.list) {
    tty_esc("1m");
    ret = crunch_escape(out,cols,wc);
    tty_esc("m");
  } else if (wc == 0x09) {
    if (out) {
      int i = TT.tabstop;
      for (;i--;) fputs(" ", out);
    }
    ret = TT.tabstop;
  }
  return ret;
}

//crunch_str with n bytes restriction for printing substrings or
//non null terminated strings
int crunch_nstr(char **str, int width, int n, FILE *out, char *escmore,
  int (*escout)(FILE *out, int cols, int wc))
{
  int columns = 0, col, bytes;
  char *start, *end;

  for (end = start = *str; *end && n>0; columns += col, end += bytes, n -= bytes) {
    wchar_t wc;

    if ((bytes = utf8towc(&wc, end, 4))>0 && (col = wcwidth(wc))>=0) {
      if (!escmore || wc>255 || !strchr(escmore, wc)) {
        if (width-columns<col) break;
        if (out) fwrite(end, bytes, 1, out);

        continue;
      }
    }

    if (bytes<1) {
      bytes = 1;
      wc = *end;
    }
    col = width-columns;
    if (col<1) break;
    if (escout) {
      if ((col = escout(out, col, wc))<0) break;
    } else if (out) fwrite(end, 1, bytes, out);
  }
  *str = end;

  return columns;
}

static void draw_page()
{
  struct linelist *scr_buf = 0;
  unsigned y = 0;
  int x = 0;
  wchar_t wc;

  char *line = 0, *end = 0;
  int utf_l = 0,  bytes = 0;

  //screen coordinates for cursor
  int cy_scr = 0, cx_scr = 0;

  //variables used only for cursor handling
  int aw = 0, iw = 0, clip = 0, margin = 8;

  int scroll = 0, redraw = 0;

  adjust_screen_buffer();
  scr_buf = screen;
  redraw = (TT.vi_mov_flag & 0x30000000)>>28;

  scroll = TT.drawn_row-TT.scr_row;
  if (TT.drawn_row<0 || TT.cur_row<0 || TT.scr_row<0) redraw = 3;
  else if (abs(scroll)>TT.screen_height/2) redraw = 3;

  tty_jump(0, 0);
  if (redraw&2) tty_esc("2J"), tty_esc("H");   //clear screen
  else if (scroll>0) printf("\033[%dL", scroll);  //scroll up
  else if (scroll<0) printf("\033[%dM", -scroll); //scroll down

  //jump until cursor
  for (; y < TT.screen_height; y++ ) {
    if (scr_buf == c_r) break;
    scr_buf = scr_buf->down;
  }
  //draw cursor row
  /////////////////////////////////////////////////////////////
  //for long lines line starts to scroll when cursor hits margin
  line = scr_buf->line->data;
  bytes = TT.cur_col;
  end = line;


  tty_jump(0, y);
  tty_esc("2K");
  //find cursor position
  aw = crunch_nstr(&end, 1024, bytes, 0, "\t", vi_crunch);

  //if we need to render text that is not inserted to buffer yet
  if (TT.vi_mode == 2 && il->len) {
    char* iend = il->data; //input end
    x = 0;
    //find insert end position
    iw = crunch_str(&iend, 1024, 0, "\t", vi_crunch);
    clip = (aw+iw) - TT.screen_width+margin;

    //if clipped area is bigger than text before insert
    if (clip > aw) {
      clip -= aw;
      iend = il->data;

      iw -= crunch_str(&iend, clip, 0, "\t", vi_crunch);
      x = crunch_str(&iend, iw, stdout, "\t", vi_crunch);
    } else {
      iend = il->data;
      end = line;

      //if clipped area is substring from cursor row start
      aw -= crunch_nstr(&end, clip, bytes, 0, "\t", vi_crunch);
      x = crunch_str(&end, aw,  stdout, "\t", vi_crunch);
      x += crunch_str(&iend, iw, stdout, "\t", vi_crunch);
    }
  }
  //when not inserting but still need to keep cursor inside screen
  //margin area
  else if ( aw+margin > TT.screen_width) {
    clip = aw-TT.screen_width+margin;
    end = line;
    aw -= crunch_nstr(&end, clip, bytes, 0, "\t", vi_crunch);
    x = crunch_str(&end, aw,  stdout, "\t", vi_crunch);
  }
  else {
    end = line;
    x = crunch_nstr(&end, aw, bytes, stdout, "\t", vi_crunch);
  }
  cx_scr = x;
  cy_scr = y;
  if (scr_buf->line->len > bytes) {
    x += crunch_str(&end, TT.screen_width-x,  stdout, "\t", vi_crunch);
  }

  if (scr_buf) scr_buf = scr_buf->down;
  // drawing cursor row ends
  ///////////////////////////////////////////////////////////////////

  //start drawing all other rows that needs update
  ///////////////////////////////////////////////////////////////////
  y = 0, scr_buf = screen;

  //if we moved around in long line might need to redraw everything
  if (clip != TT.drawn_col) redraw = 3;

  for (; y < TT.screen_height; y++ ) {
    int draw_line = 0;
    if (scr_buf == c_r) {
      scr_buf = scr_buf->down;
      continue;
    } else if (redraw) draw_line++;
    else if (scroll<0 && TT.screen_height-y-1<-scroll)
      scroll++, draw_line++;
    else if (scroll>0) scroll--, draw_line++;

    tty_jump(0, y);
    if (draw_line) {

      tty_esc("2K");
      if (scr_buf) {
        if (draw_line && scr_buf->line->data && scr_buf->line->len) {
          line = scr_buf->line->data;
          bytes = scr_buf->line->len;

          aw = crunch_nstr(&line, clip, bytes, 0, "\t", vi_crunch);
          crunch_str(&line, TT.screen_width-1, stdout, "\t", vi_crunch);
          if ( *line ) printf("@");

        }
      } else if (draw_line) printf("~");
    }
    if (scr_buf) scr_buf = scr_buf->down;
  }

  TT.drawn_row = TT.scr_row, TT.drawn_col = clip;

  //finished updating visual area

  tty_jump(0, TT.screen_height);
  tty_esc("2K");
  if (TT.vi_mode == 2) printf("\x1b[1m-- INSERT --\x1b[m");

  //DEBUG
  utf_l=utf8towc(&wc, &c_r->line->data[TT.cur_col], c_r->line->len-TT.cur_col);
  if (utf_l > 1) {
    char t[5] = {0, 0, 0, 0, 0};
    strncpy(t, &c_r->line->data[TT.cur_col], utf_l);
    printf(" (utf: %d %s)", utf_l, t);
  }
  //DEBUG

  tty_jump(TT.screen_width-12, TT.screen_height);
  printf("%d,%d", TT.cur_row+1, TT.cur_col+1);
  if (TT.cur_col != cx_scr) printf("-%d", cx_scr+1);
  putchar('\n');

  tty_esc("m");
  tty_jump(0, TT.screen_height+1);
  tty_esc("2K");
  if (!TT.vi_mode) {
    tty_esc("1m");
    printf("%s", il->data);
    tty_esc("m");
  } else tty_jump(cx_scr, cy_scr);

  xflush(1);

}

static void check_cursor_bounds()
{
  if (c_r->line->len == 0) {
    TT.cur_col = 0;
    return;
  } else if (c_r->line->len-1 < TT.cur_col) TT.cur_col = c_r->line->len-1;

  if (TT.cur_col && utf8_width(&c_r->line->data[TT.cur_col],
        c_r->line->len-TT.cur_col) <= 0)
    TT.cur_col--, check_cursor_bounds();
}

static void adjust_screen_buffer()
{
  //search cursor and screen
  struct linelist *t = text;
  int c = -1, s = -1, i = 0;
  //searching cursor and screen line numbers
  for (;((c == -1) || (s == -1)) && t != 0; i++, t = t->down) {
    if (t == c_r) c = i;
    if (t == screen) s = i;
  }
  //adjust screen buffer so cursor is on drawing area
  if (c <= s) screen = c_r, s = c; //scroll up
  else {
    //drawing does not have wrapping so no need to check width
    int distance = c-s+1;

    if (distance > (int)TT.screen_height) {
      int adj = distance-TT.screen_height;
      for (;adj; adj--) screen = screen->down, s++; //scroll down

    }
  }
  TT.cur_row = c, TT.scr_row = s;

}

//get utf8 length and width at same time
static int utf8_lnw(int* width, char* s, int bytes)
{
  wchar_t wc;
  int length;

  *width = 0;
  if (*s == '\t') {
    *width = TT.tabstop;
    return 1;
  }
  length = utf8towc(&wc, s, bytes);
  if (length < 1) return 0;
  *width = wcwidth(wc);
  return length;
}

//try to estimate width of next "glyph" in terminal buffer
//combining chars 0x300-0x36F shall be zero width
static int utf8_width(char *s, int bytes)
{
  wchar_t wc;
  int length;

  if (*s == '\t') return TT.tabstop;
  length = utf8towc(&wc, s, bytes);
  if (length < 1) return -1;
  return wcwidth(wc);
}

static int utf8_dec(char key, char *utf8_scratch, int *sta_p)
{
  int len = 0;
  char *c = utf8_scratch;
  c[*sta_p] = key;
  if (!(*sta_p))  *c = key;
  if (*c < 0x7F) { *sta_p = 1; return 1; }
  if ((*c & 0xE0) == 0xc0) len = 2;
  else if ((*c & 0xF0) == 0xE0 ) len = 3;
  else if ((*c & 0xF8) == 0xF0 ) len = 4;
  else {*sta_p = 0; return 0; }

  (*sta_p)++;

  if (*sta_p == 1) return 0;
  if ((c[*sta_p-1] & 0xc0) != 0x80) {*sta_p = 0; return 0; }

  if (*sta_p == len) { c[(*sta_p)] = 0; return 1; }

  return 0;
}

static char* utf8_last(char* str, int size)
{
  char* end = str+size;
  int pos = size;
  int len = 0;
  int width = 0;
  while (pos >= 0) {
    len = utf8_lnw(&width, end, size-pos);
    if (len && width) return end;
    end--; pos--;
  }
  return 0;
}

static int cur_left(int count0, int count1, char* unused)
{
  int count = count0*count1;
  TT.vi_mov_flag |= 0x80000000;
  for (;count--;) {
    if (!TT.cur_col) return 1;

    TT.cur_col--;
    check_cursor_bounds();
  }
  return 1;
}

static int cur_right(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count--;) {
    if (c_r->line->len <= 1) return 1;
    if (TT.cur_col >= c_r->line->len-1) {
      TT.cur_col = utf8_last(c_r->line->data, c_r->line->len)
        - c_r->line->data;
      return 1;
    }
    TT.cur_col++;
    if (utf8_width(&c_r->line->data[TT.cur_col],
          c_r->line->len-TT.cur_col) <= 0)
      cur_right(1, 1, 0);
  }
  return 1;
}

static int cur_up(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count-- && c_r->up;)
    c_r = c_r->up;

  TT.vi_mov_flag |= 0x80000000;
  check_cursor_bounds();
  return 1;
}

static int cur_down(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count-- && c_r->down;)
    c_r = c_r->down;

  check_cursor_bounds();
  return 1;
}

