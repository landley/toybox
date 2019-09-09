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
)

/*
 *
 * TODO:
 * BUGS:  screen pos adjust does not cover "widelines"
 *
 *
 * REFACTOR:  use dllist functions where possible.
 *            draw_page dont draw full page at time if nothing changed...
 *            ex callbacks
 *
 * FEATURE:   ex: / ? %   //atleast easy cases
 *            ex: r
 *            ex: !external programs
 *            ex: w filename //only writes to same file now
 *            big file support?
 */


struct linestack_show {
  struct linestack_show *next;
  long top, left;
  int x, width, y, height;
};

static void draw_page();
static int draw_str_until(int *drawn, char *str, int width, int bytes);
static void draw_char(char c, int x, int y, int highlight);
//utf8 support
static int utf8_lnw(int* width, char* str, int bytes);
static int utf8_dec(char key, char *utf8_scratch, int *sta_p);
static int utf8_len(char *str);
static int utf8_width(char *str, int bytes);
static int draw_rune(char *c, int x, int y, int highlight);
static char* utf8_last(char* str, int size);


static int cur_left(int count0, int count1, char* unused);
static int cur_right(int count0, int count1, char* unused);
static int cur_up(int count0, int count1, char* unused);
static int cur_down(int count0, int count1, char* unused);
static void check_cursor_bounds();
static void adjust_screen_buffer();
static int search_str(char *s);

struct str_line {
  int alloc_len;
  int str_len;
  char *str_data;
};

//lib dllist uses next and prev kinda opposite what im used to so I just
//renamed both ends to up and down
struct linelist {
  struct linelist *up;//next
  struct linelist *down;//prev
  struct str_line *line;
};
//inserted line not yet pushed to buffer
struct str_line *il;
struct linelist *text; //file loaded into buffer
struct linelist *scr_r;//current screen coord 0 row
struct linelist *c_r;//cursor position row


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
//TODO implement
void linelist_unload()
{

}

void write_file(char *filename)
{
  struct linelist *lst = text;
  FILE *fp = 0;
  if (!filename)
    filename = (char*)*toys.optargs;
  fp = fopen(filename, "w");
  if (!fp) return;
  while (lst) {
    fprintf(fp, "%s\n", lst->line->str_data);
    lst = lst->down;
  }
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
    lst->line->alloc_len = alc;
    lst->line->str_len = 0;
    lst->line->str_data = line;
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
    lst->line->alloc_len = alc;
    lst->line->str_len = len;
    lst->line->str_data = line;

    if (lst->line->str_data[len-1] == '\n') {
      lst->line->str_data[len-1] = 0;
      lst->line->str_len--;
    }
    if (text == 0) {
      text = lst;
    }

  }
  if (text) {
    dlist_terminate(text->up);
  }
  fclose(fp);
  return 1;

}

int vi_yy(char reg, int count0, int count1)
{
  return 1;
}

//TODO this is overly complicated refactor with lib dllist
int vi_dd(char reg, int count0, int count1)
{
  int count = count0*count1;
  struct linelist *lst = c_r;
  if (c_r == text && text == scr_r) {
    if (!text->down && !text->up && text->line) {
      text->line->str_len = 1;
      sprintf(text->line->str_data, " ");
      goto success_exit;
    }
    if (text->down) {
      text = text->down;
      text->up = 0;
      c_r = text;
      scr_r = text;
      free(lst->line->str_data);
      free(lst->line);
      free(lst);
    }
    goto recursion_exit;
  }
  //TODO use lib dllist stuff
  if (lst)
  {
    if (lst->down) {
      lst->down->up = lst->up;
    }
    if (lst->up) {
      lst->up->down = lst->down;
    }
    if (scr_r == c_r) {
      scr_r = c_r->down ? c_r->down : c_r->up;
    }
    if (c_r->down)
      c_r = c_r->down;
    else {
      c_r = c_r->up;
      count = 1;
    }
    free(lst->line->str_data);
    free(lst->line);
    free(lst);
  }

recursion_exit:
  count--;
  //make this recursive
  if (count>0)
    return vi_dd(reg, count, 1);
success_exit:
  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}
//TODO i think this thing has bug when removing >40 chars from 80 wide line
static int vi_x(char reg, int count0, int count1)
{
  int count = count0;
  char *s;
  char *last;
  int *l;
  int length = 0;
  int width = 0;
  int remaining = 0;
  char *end;
  char *start;
  if (!c_r)
    return 0;
  s = c_r->line->str_data;
  l = &c_r->line->str_len;

  last = utf8_last(s,*l);
  if (last == s+TT.cur_col) {
    memset(last, 0, (*l)-TT.cur_col);
    *l = TT.cur_col;
    if (!TT.cur_col) return 1;
    last = utf8_last(s, TT.cur_col);
    TT.cur_col = last-s;
    return 1;
  }

  start = s+TT.cur_col;
  end = start;
  remaining = (*l)-TT.cur_col;
  for (;remaining;) {
    int next = utf8_lnw(&width, end, remaining);
    if (next && width) {
      if (!count) break;
      count--;
    } if (!next) break;
    length += next;
    end += next;
    remaining -= next;
  }
  if (remaining) {
    memmove(start, end, remaining);
    memset(start+remaining,0,end-start);
  } else {
    memset(start,0,(*l)-TT.cur_col);
  }
  *l -= end-start;
  if (!TT.cur_col) return 1;
  if (TT.cur_col == (*l)) {
    last = utf8_last(s, TT.cur_col);
    TT.cur_col = last-s;
  }
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
  if (TT.cur_col == c_r->line->str_len-1 || !c_r->line->str_len)
    goto next_line;
  if (strchr(empties, c_r->line->str_data[TT.cur_col]))
    goto find_non_empty;
  if (strchr(specials, c_r->line->str_data[TT.cur_col])) {
    for (;strchr(specials, c_r->line->str_data[TT.cur_col]); ) {
      TT.cur_col++;
      if (TT.cur_col == c_r->line->str_len-1)
        goto next_line;
    }
  } else for (;!strchr(specials, c_r->line->str_data[TT.cur_col]) &&
      !strchr(empties, c_r->line->str_data[TT.cur_col]);) {
      TT.cur_col++;
      if (TT.cur_col == c_r->line->str_len-1)
        goto next_line;
  }

  for (;strchr(empties, c_r->line->str_data[TT.cur_col]); ) {
    TT.cur_col++;
find_non_empty:
    if (TT.cur_col == c_r->line->str_len-1) {
next_line:
      //we could call j and g0
      if (!c_r->down) return 0;
      c_r = c_r->down;
      TT.cur_col = 0;
      if (!c_r->line->str_len) break;
    }
  }
  count--;
  if (count>0)
    return vi_movw(count, 1, 0);

  check_cursor_bounds();
  adjust_screen_buffer();
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
      TT.cur_col = (c_r->line->str_len) ? c_r->line->str_len-1 : 0;
      goto exit_function;
  }
  if (TT.cur_col)
      TT.cur_col--;
  while (c_r->line->str_data[TT.cur_col] <= ' ') {
    if (TT.cur_col) TT.cur_col--;
    else goto exit_function;
  }
  while (c_r->line->str_data[TT.cur_col] > ' ') {
    if (TT.cur_col)TT.cur_col--;
    else goto exit_function;
  }
  TT.cur_col++;
exit_function:
  count--;
  if (count>1)
    return vi_movb(count, 1, 0);
  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

static int vi_move(int count0, int count1, char *unused)
{
  int count = count0*count1;
  if (!c_r)
    return 0;
  if (TT.cur_col < c_r->line->str_len)
    TT.cur_col++;
  if (c_r->line->str_data[TT.cur_col] <= ' ' || count > 1)
    vi_movw(count, 1, 0); //find next word;
  while (c_r->line->str_data[TT.cur_col] > ' ')
    TT.cur_col++;
  if (TT.cur_col) TT.cur_col--;

  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

void i_insert()
{
  char *t = xzalloc(c_r->line->alloc_len);
  char *s = c_r->line->str_data;
  int sel = c_r->line->str_len-TT.cur_col;
  strncpy(t, &s[TT.cur_col], sel);
  t[sel+1] = 0;
  if (c_r->line->alloc_len < c_r->line->str_len+il->str_len+5) {
    c_r->line->str_data = xrealloc(c_r->line->str_data,
      c_r->line->alloc_len*2+il->alloc_len*2);

    c_r->line->alloc_len = c_r->line->alloc_len*2+2*il->alloc_len;
    memset(&c_r->line->str_data[c_r->line->str_len], 0,
        c_r->line->alloc_len-c_r->line->str_len);

    s = c_r->line->str_data;
  }
  strcpy(&s[TT.cur_col], il->str_data);
  strcpy(&s[TT.cur_col+il->str_len], t);
  TT.cur_col += il->str_len;
  if (TT.cur_col) TT.cur_col--;
  c_r->line->str_len += il->str_len;
  free(t);

}

//new line at split pos;
void i_split()
{
  struct str_line *l = xmalloc(sizeof(struct str_line));
  int l_a = c_r->line->alloc_len;
  int l_len = c_r->line->str_len-TT.cur_col;
  l->str_data = xzalloc(l_a);
  l->alloc_len = l_a;
  l->str_len = l_len;
  strncpy(l->str_data, &c_r->line->str_data[TT.cur_col], l_len);
  l->str_data[l_len] = 0;
  c_r->line->str_len -= l_len;
  c_r->line->str_data[c_r->line->str_len] = 0;
  c_r = (struct linelist*)dlist_insert((struct double_list**)&c_r, (char*)l);
  c_r->line = l;
  TT.cur_col = 0;
  check_cursor_bounds();
  adjust_screen_buffer();
}

static int vi_zero(int count0, int count1, char *unused)
{
  TT.cur_col = 0;
  return 1;
}

static int vi_eol(int count0, int count1, char *unused)
{
  int count = count0*count1;
  for (;count > 1 && c_r->down; count--)
    c_r = c_r->down;

  if (c_r && c_r->line->str_len)
    TT.cur_col = c_r->line->str_len-1;
  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  return 1;
}

static int vi_find_c(int count0, int count1, char *symbol)
{
  int count = count0*count1;
  if (c_r && c_r->line->str_len) {
    while (count--) {
        char* pos = strstr(&c_r->line->str_data[TT.cur_col], symbol);
        if (pos) {
          TT.cur_col = pos-c_r->line->str_data;
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
  c_r = text;
  while(--count0) {
    if (c_r && c_r->down) c_r = c_r->down;
  }
  TT.cur_col = 0;
  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

//need to refactor when implementing yank buffers
static int vi_delete(char reg, struct linelist *row, int col, int flags)
{
  if (row == c_r) {
    if (col < TT.cur_col) {
      int distance = TT.cur_col - col;
      TT.cur_col = col;
      vi_x(reg, distance, 1);
    } else {
      int distance = col - TT.cur_col;
      if (distance > 0) vi_x(reg, distance, 1);
    }
    if (TT.vi_mov_flag&2) 
      vi_x(reg, 1, 1);
  }
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
  return 1;
}

static int vi_join(char reg, int count0, int count1)
{
  while (count0--) {
    if (c_r && c_r->down) {
      int size = c_r->line->str_len+c_r->down->line->str_len;
      if (size > c_r->line->alloc_len) {
        if (size > c_r->down->line->alloc_len) {
          c_r->line->str_data = xrealloc(c_r->line->str_data,
            c_r->line->alloc_len*2+il->alloc_len*2);
          memmove(&c_r->line->str_data[c_r->line->str_len],
              c_r->down->line->str_data,c_r->down->line->str_len);
          c_r->line->str_len = size;
          c_r = c_r->down;
          c_r->line->alloc_len = c_r->line->alloc_len*2+2*il->alloc_len;
          vi_dd(0,1,1);
        } else {
          memmove(&c_r->down->line->str_data[c_r->line->str_len],
              c_r->down->line->str_data,c_r->down->line->str_len);
          memmove(c_r->down->line->str_data,c_r->line->str_data,
              c_r->line->str_len);
          c_r->down->line->str_len = size;
          vi_dd(0,1,1);
        }
      } else {
          memmove(&c_r->line->str_data[c_r->line->str_len],
              c_r->down->line->str_data,c_r->down->line->str_len);
          c_r->line->str_len = size;
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

static int vi_yank(char reg, struct linelist *row, int col, int flags)
{
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
  int i = 0;
  int val = 0;
  char *cmd_e;
  int (*vi_cmd)(char, struct linelist*, int, int) = 0;
  int (*vi_mov)(int, int, char*) = 0;
  TT.count0 = 0;
  TT.count1 = 0;
  TT.vi_reg = '"';
  TT.vi_mov_flag = 0;
  if (*cmd == '"') {
    cmd++;
    TT.vi_reg = *cmd; //TODO check validity
    cmd++;
  }
  val = strtol(cmd, &cmd_e, 10);
  if (errno || val == 0) val = 1;
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
  val = strtol(cmd, &cmd_e, 10);
  if (errno || val == 0) val = 1;
  else cmd = cmd_e;
  TT.count1 = val;

  for (i = 0; i < ARRAY_LEN(vi_movs); i++) {
    if (!strncmp(cmd, vi_movs[i].mov, strlen(vi_movs[i].mov))) {
      vi_mov = vi_movs[i].vi_mov;
      TT.vi_mov_flag = vi_movs[i].flags;
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
  char *c = strstr(&c_r->line->str_data[TT.cur_col+1], s);

  if (TT.last_search != s) {
    free(TT.last_search);
    TT.last_search = xstrdup(s);
  }

  if (c) {
    TT.cur_col = c-c_r->line->str_data;
  } else for (; !c;) {
    lst = lst->down;
    if (!lst) return 1;
    c = strstr(lst->line->str_data, s);
  }
  c_r = lst;
  TT.cur_col = c-c_r->line->str_data;
  check_cursor_bounds();
  adjust_screen_buffer();
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
  }
  return 0;

}

void vi_main(void)
{
  char keybuf[16];
  char utf8_code[8];
  int utf8_dec_p = 0;
  char vi_buf[16];
  int vi_buf_pos = 0;
  il = xzalloc(sizeof(struct str_line));
  il->str_data = xzalloc(80);
  il->alloc_len = 80;
  keybuf[0] = 0;
  memset(vi_buf, 0, 16);
  memset(utf8_code, 0, 8);
  linelist_load(0);
  scr_r = text;
  c_r = text;
  TT.cur_row = 0;
  TT.cur_col = 0;
  TT.screen_width = 80;
  TT.screen_height = 24;
  TT.vi_mode = 1;
  TT.tabstop = 8;
  terminal_size(&TT.screen_width, &TT.screen_height);
  TT.screen_height -= 2; //TODO this is hack fix visual alignment
  set_terminal(0, 1, 0, 0);
  //writes stdout into different xterm buffer so when we exit
  //we dont get scroll log full of junk
  tty_esc("?1049h");
  tty_esc("H");
  xflush(1);
  draw_page();
  while(1) {
    int key = scan_key(keybuf, -1);

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

    switch (key) {
      case -1:
      case 3:
      case 4:
        goto cleanup_vi;
    }
    if (TT.vi_mode == 1) { //NORMAL
      switch (key) {
        case '/':
        case '?':
        case ':':
          TT.vi_mode = 0;
          il->str_data[0]=key;
          il->str_len++;
          break;
        case 'A':
          vi_eol(1, 1, 0);
          // FALLTHROUGH
        case 'a':
          if (c_r && c_r->line->str_len) TT.cur_col++;
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
          if (il->str_len > 1) {
            il->str_data[--il->str_len] = 0;
            break;
          }
          // FALLTHROUGH
        case 27:
          TT.vi_mode = 1;
          il->str_len = 0;
          memset(il->str_data, 0, il->alloc_len);
          break;
        case 0x0D:
          if (run_ex_cmd(il->str_data) == -1)
            goto cleanup_vi;
          TT.vi_mode = 1;
          il->str_len = 0;
          memset(il->str_data, 0, il->alloc_len);
          break;
        default: //add chars to ex command until ENTER
          if (key >= 0x20 && key < 0x7F) { //might be utf?
            if (il->str_len == il->alloc_len) {
              il->str_data = realloc(il->str_data, il->alloc_len*2);
              il->alloc_len *= 2;
            }
            il->str_data[il->str_len] = key;
            il->str_len++;
          }
          break;
      }
    } else if (TT.vi_mode == 2) {//INSERT MODE
      switch (key) {
        case 27:
          i_insert();
          TT.vi_mode = 1;
          il->str_len = 0;
          memset(il->str_data, 0, il->alloc_len);
          break;
        case 0x7F:
        case 0x08:
          if (il->str_len)
            il->str_data[il->str_len--] = 0;
          break;
        case 0x09:
          il->str_data[il->str_len++] = '\t';
          break;

        case 0x0D:
          //insert newline
          //
          i_insert();
          il->str_len = 0;
          memset(il->str_data, 0, il->alloc_len);
          i_split();
          break;
        default:
          if (key >= 0x20 && utf8_dec(key, utf8_code, &utf8_dec_p)) {
            if (il->str_len+utf8_dec_p+1 >= il->alloc_len) {
              il->str_data = realloc(il->str_data, il->alloc_len*2);
              il->alloc_len *= 2;
            }
            strcpy(il->str_data+il->str_len, utf8_code);
            il->str_len += utf8_dec_p;
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
  tty_reset();
  tty_esc("?1049l");
}

static void draw_page()
{
  unsigned y = 0;
  int cy_scr = 0;
  int cx_scr = 0;
  int utf_l = 0;

  char* line = 0;
  int bytes = 0;
  int drawn = 0;
  int x = 0;
  struct linelist *scr_buf= scr_r;
  //clear screen
  tty_esc("2J");
  tty_esc("H");

  tty_jump(0, 0);

  //draw lines until cursor row
  for (; y < TT.screen_height; ) {
    if (line && bytes) {
      draw_str_until(&drawn, line, TT.screen_width, bytes);
      bytes = drawn ? (bytes-drawn) : 0;
      line = bytes ? (line+drawn) : 0;
      y++;
      tty_jump(0, y);
    } else if (scr_buf && scr_buf->line->str_data && scr_buf->line->str_len) {
      if (scr_buf == c_r)
        break;
      line = scr_buf->line->str_data;
      bytes = scr_buf->line->str_len;
      scr_buf = scr_buf->down;
    } else {
      if (scr_buf == c_r)
        break;
      y++;
      tty_jump(0, y);
      //printf(" \n");
      if (scr_buf) scr_buf = scr_buf->down;
    }

  }
  //draw cursor row until cursor
  //this is to calculate cursor position on screen and possible insert
  line = scr_buf->line->str_data;
  bytes = TT.cur_col;
  for (; y < TT.screen_height; ) {
    if (bytes) {
      x = draw_str_until(&drawn, line, TT.screen_width, bytes);
      bytes = drawn ? (bytes-drawn) : 0;
      line = bytes ? (line+drawn) : 0;
    }
    if (!bytes) break;
    y++;
    tty_jump(0, y);
  }
  if (TT.vi_mode == 2 && il->str_len) {
    line = il->str_data;
    bytes = il->str_len;
    cx_scr = x;
    cy_scr = y;
    x = draw_str_until(&drawn, line, TT.screen_width-x, bytes);
    bytes = drawn ? (bytes-drawn) : 0;
    line = bytes ? (line+drawn) : 0;
    cx_scr += x;
    for (; y < TT.screen_height; ) {
      if (bytes) {
        x = draw_str_until(&drawn, line, TT.screen_width, bytes);
        bytes = drawn ? (bytes-drawn) : 0;
        line = bytes ? (line+drawn) : 0;
        cx_scr = x;
      }
      if (!bytes) break;
      y++;
      cy_scr = y;
      tty_jump(0, y);
    }
  } else {
    cy_scr = y;
    cx_scr = x;
  }
  line = scr_buf->line->str_data+TT.cur_col;
  bytes = scr_buf->line->str_len-TT.cur_col;
  scr_buf = scr_buf->down;
  x = draw_str_until(&drawn,line, TT.screen_width-x, bytes);
  bytes = drawn ? (bytes-drawn) : 0;
  line = bytes ? (line+drawn) : 0;
  y++;
  tty_jump(0, y);

//draw until end
  for (; y < TT.screen_height; ) {
    if (line && bytes) {
      draw_str_until(&drawn, line, TT.screen_width, bytes);
      bytes = drawn ? (bytes-drawn) : 0;
      line = bytes ? (line+drawn) : 0;
      y++;
      tty_jump(0, y);
    } else if (scr_buf && scr_buf->line->str_data && scr_buf->line->str_len) {
      line = scr_buf->line->str_data;
      bytes = scr_buf->line->str_len;
      scr_buf = scr_buf->down;
    } else {
      y++;
      tty_jump(0, y);
      if (scr_buf) scr_buf = scr_buf->down;
    }

  }

  tty_jump(0, TT.screen_height);
  switch (TT.vi_mode) {
    case 0:
    tty_esc("30;44m");
    printf("COMMAND|");
    break;
    case 1:
    tty_esc("30;42m");
    printf("NORMAL|");
    break;
    case 2:
    tty_esc("30;41m");
    printf("INSERT|");
    break;

  }
  //DEBUG
  tty_esc("m");
  utf_l = utf8_len(&c_r->line->str_data[TT.cur_col]);
  if (utf_l) {
    char t[5] = {0, 0, 0, 0, 0};
    strncpy(t, &c_r->line->str_data[TT.cur_col], utf_l);
    printf("utf: %d %s", utf_l, t);
  }
  printf("| %d, %d\n", cx_scr, cy_scr); //screen coord

  tty_jump(TT.screen_width-12, TT.screen_height);
  printf("| %d, %d\n", TT.cur_row, TT.cur_col);
  tty_esc("m");
  if (!TT.vi_mode) {
    tty_esc("1m");
    tty_jump(0, TT.screen_height+1);
    printf("%s", il->str_data);
    tty_esc("m");
  } else tty_jump(cx_scr, cy_scr);

  xflush(1);

}

static void draw_char(char c, int x, int y, int highlight)
{
  tty_jump(x, y);
  if (highlight) {
    tty_esc("30m"); //foreground black
    tty_esc("47m"); //background white
  }
  printf("%c", c);
}

//utf rune draw
//printf and useless copy could be replaced by direct write() to stdout
static int draw_rune(char *c, int x, int y, int highlight)
{
  int l = utf8_len(c);
  char t[5] = {0, 0, 0, 0, 0};
  if (!l) return 0;
  tty_jump(x, y);
  tty_esc("0m");
  if (highlight) {
    tty_esc("30m"); //foreground black
    tty_esc("47m"); //background white
  }
  strncpy(t, c, 5);
  printf("%s", t);
  tty_esc("0m");
  return l;
}

static void check_cursor_bounds()
{
  if (c_r->line->str_len == 0) TT.cur_col = 0;
  else if (c_r->line->str_len-1 < TT.cur_col) TT.cur_col = c_r->line->str_len-1;
  if (utf8_width(&c_r->line->str_data[TT.cur_col], c_r->line->str_len-TT.cur_col) <= 0)
    cur_left(1, 1, 0);
}

static void adjust_screen_buffer()
{
  //search cursor and screen TODO move this perhaps
  struct linelist *t = text;
  int c = -1;
  int s = -1;
  int i = 0;
  for (;;) {
    i++;
    if (t == c_r)
      c = i;
    if (t == scr_r)
      s = i;
    t = t->down;
    if ( ((c != -1) && (s != -1)) || t == 0)
      break;
  }
  if (c <= s) {
    scr_r = c_r;
  }
  else if ( c > s ) {
    //should count multiline long strings!
    int distance = c - s + 1;
    //TODO instead iterate scr_r up and check strlen%screen_width
    //for each iteration
    if (distance >= (int)TT.screen_height) {
      int adj = distance - TT.screen_height;
      while (adj--) {
        scr_r = scr_r->down;
      }
    }
  }
  TT.cur_row = c;

}

//return 0 if not ASCII nor UTF-8
//this is not fully tested
//naive implementation with branches
//there is better branchless lookup table versions out there
//1 0xxxxxxx
//2 110xxxxx  10xxxxxx
//3 1110xxxx  10xxxxxx  10xxxxxx
//4 11110xxx  10xxxxxx  10xxxxxx  10xxxxxx
static int utf8_len(char *str)
{
  int len = 0;
  int i = 0;
  uint8_t *c = (uint8_t*)str;
  if (!c || !(*c)) return 0;
  if (*c < 0x7F) return 1;
  if ((*c & 0xE0) == 0xc0) len = 2;
  else if ((*c & 0xF0) == 0xE0 ) len = 3;
  else if ((*c & 0xF8) == 0xF0 ) len = 4;
  else return 0;
  c++;
  for (i = len-1; i > 0; i--) {
    if ((*c++ & 0xc0) != 0x80) return 0;
  }
  return len;
}

//get utf8 length and width at same time
static int utf8_lnw(int* width, char* str, int bytes)
{
  wchar_t wc;
  int length = 1;
  *width = 1;
  if (*str == 0x09) {
    *width = TT.tabstop;
    return 1;
  }
  length = mbtowc(&wc, str, bytes);
  switch (length) {
  case -1:
    mbtowc(0,0,4);
  case 0:
    *width = 0;
    length = 0;
    break;
  default:
  *width = wcwidth(wc);
  }
  return length;
}

//try to estimate width of next "glyph" in terminal buffer
//combining chars 0x300-0x36F shall be zero width
static int utf8_width(char *str, int bytes)
{
  wchar_t wc;
  if (*str == 0x09) return TT.tabstop;
  switch (mbtowc(&wc, str, bytes)) {
  case -1:
    mbtowc(0,0,4);
  case 0:
    return -1;
  default:
  return wcwidth(wc);
  }
  return 0;
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

static int draw_str_until(int *drawn, char *str, int width, int bytes)
{
  int len = 0;
  int rune_width = 0;
  int rune_bytes = 0;
  int max_bytes = bytes;
  int max_width = width;
  char* end = str;
  for (;width && bytes;) {
    if (*end == 0x09) {
      rune_bytes = 1;
      rune_width = TT.tabstop;
    } else rune_bytes = utf8_lnw(&rune_width, end, 4);

    if (!rune_bytes) break;
    if (width - rune_width < 0) goto write_bytes;
    width -= rune_width;
    bytes -= rune_bytes;
    end += rune_bytes;
  }
  for (;bytes;) {
    if (*end == 0x09) {
      rune_bytes = 1;
      rune_width = TT.tabstop;
    } else rune_bytes = utf8_lnw(&rune_width, end, 4);

    if (!rune_bytes) break;
    if (rune_width) break;
    bytes -= rune_bytes;
    end += rune_bytes;
  }
write_bytes:
  len = max_bytes-bytes;
  for (;len--; str++) {
    if (*str == 0x09) {
      int i = 8;
      for (;i--;) fwrite(" ", 1, 1, stdout);
    } else fwrite(str, 1, 1, stdout);
  }
  *drawn = max_bytes-bytes;
  return max_width-width;
}

static int cur_left(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count--;) {
    if (!TT.cur_col) return 1;

    TT.cur_col--;
    check_cursor_bounds();//has bit ugly recursion hidden here
  }
  return 1;
}

static int cur_right(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count--;) {
    if (c_r->line->str_len <= 1) return 1;
    if (TT.cur_col >= c_r->line->str_len-1) {
      TT.cur_col = utf8_last(c_r->line->str_data, c_r->line->str_len)
        - c_r->line->str_data;
      return 1;
    }
    TT.cur_col++;
    if (utf8_width(&c_r->line->str_data[TT.cur_col],
          c_r->line->str_len-TT.cur_col) <= 0)
      cur_right(1, 1, 0);
  }
  return 1;
}

static int cur_up(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count-- && c_r->up;)
    c_r = c_r->up;

  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

static int cur_down(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count-- && c_r->down;)
    c_r = c_r->down;

  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

