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
    struct termios default_opts;
    struct linestack *ls;
    char *statline;
    int cur_col;
    int cur_row;
    unsigned screen_height;
    unsigned screen_width;
    int vi_mode;
)

/*
 *
 * TODO:
 * BUGS:  screen pos adjust does not cover "widelines"
 *        utf8 problems with some files. perhaps use lib utf8 functions instead
 *        append to EOL does not show input but works when ESC out
 *        
 *
 * REFACTOR:  use dllist functions where possible.
 *            draw_page dont draw full page at time if nothing changed...
 *            ex callbacks
 *            
 * FEATURE:   ex: / ? %   //atleast easy cases
 *            vi: x dw d$ d0 
 *            vi: yw yy (y0 y$)
 *            vi+ex: gg G //line movements
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
static void draw_char(char c, int x, int y, int highlight);
//utf8 support
static int utf8_dec(char key, char* utf8_scratch,int* sta_p) ;
static int utf8_len(char* str);
static int draw_rune(char* c,int x,int y, int highlight);


static void cur_left();
static void cur_right();
static void cur_up();
static void cur_down();
static void check_cursor_bounds();
static void adjust_screen_buffer();


struct str_line {
  int alloc_len;
  int str_len; 
  char* str_data;
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
int modified;

void dlist_insert_nomalloc(struct double_list **list, struct double_list *new)
{
  if (*list) {
    new->next = *list;
    new->prev = (*list)->prev;
    if((*list)->prev) (*list)->prev->next = new;
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
void linelist_unload() 
{
 
}

void write_file(char* filename)
{
  struct linelist *lst = text;
  FILE *fp = 0;
  if (!filename)
    filename = (char*)*toys.optargs;
  fp = fopen(filename,"w");
  if (!fp) return ;
  while(lst) {
    fprintf(fp,"%s\n",lst->line->str_data);
    lst = lst->down;
  }
  fclose(fp);
}

int linelist_load(char* filename) 
{
  struct linelist *lst = c_r;//cursor position or 0
  FILE* fp = 0;
  if (!filename)
    filename = (char*)*toys.optargs;

  fp = fopen(filename, "r");
  if (!fp) return 0;


  for (;;) {
    char* line = xzalloc(80);
    ssize_t alc =80;
    ssize_t len;
    if ((len = getline(&line, (void *)&alc, fp))== -1) {
      if (errno == EINVAL || errno == ENOMEM) {
        printf("error %d\n",errno);
      }
      free(line);
      break;
    }
    lst = (struct linelist*)dlist_add((struct double_list**)&lst,
        xzalloc(sizeof(struct str_line)));
    lst->line->alloc_len = alc;
    lst->line->str_len = len;
    lst->line->str_data = line;

    if (lst->line->str_data[len-1]=='\n') {
      lst->line->str_data[len-1]=0;
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
//TODO this is overly complicated refactor with lib dllist
int ex_dd(int count) 
{
  struct linelist* lst = c_r;
  if (c_r==text && text == scr_r) {
    if (!text->down && !text->up && text->line) {
      text->line->str_len=1;
      sprintf(text->line->str_data," ");
      goto success_exit;
    } 
    if (text->down) {
      text =text->down;
      text->up=0;
      c_r=text;
      scr_r=text;
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
      scr_r =c_r->down ? c_r->down : c_r->up; 
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
  if (count)
    return ex_dd(count);
success_exit:
  check_cursor_bounds();
  adjust_screen_buffer();
  return 1;
}

int ex_dw(int count)
{
  return 1;
}

int ex_deol(int count) 
{
  return 1;
}
//does not work with utf8 yet
int vi_x(int count)
{
  char* s;
  int* l;
  int* p;
  if (!c_r)
    return 0;
  s = c_r->line->str_data;
  l = &c_r->line->str_len;
  p = &TT.cur_col;
  if (!(*l)) return 0;
  if ((*p) == (*l)-1) { 
    s[*p]=0;
    if (*p) (*p)--; 
    (*l)--;
  } else { 
    memmove(s+(*p),s+(*p)+1,(*l)-(*p)); 
    s[*l]=0; 
    (*l)--; 
  }
  count--;
  return (count) ? vi_x(count) : 1; 
}

//move commands does not behave correct way yet.
//only jump to next space for now.
int vi_movw(int count) 
{
  if (!c_r)
    return 0;
  //could we call moveend first
  while(c_r->line->str_data[TT.cur_col] > ' ') 
    TT.cur_col++;
  while(c_r->line->str_data[TT.cur_col] <= ' ') {
    TT.cur_col++;
    if (!c_r->line->str_data[TT.cur_col]) {
      //we could call j and g0
      if (!c_r->down) return 0;
      c_r = c_r->down;
      TT.cur_col=0;
    }
  } 
  count--;
  if (count>1)
    return vi_movw(count);

      check_cursor_bounds();
      adjust_screen_buffer();
  return 1;
}

int vi_movb(int count) 
{
  if (!c_r)
    return 0;
  if (!TT.cur_col) {
      if (!c_r->up) return 0;
      c_r = c_r->up;
      TT.cur_col=(c_r->line->str_len) ? c_r->line->str_len-1 : 0;
      goto exit_function;
  }
  if (TT.cur_col)
      TT.cur_col--;
  while(c_r->line->str_data[TT.cur_col] <= ' ') {
    if (TT.cur_col) TT.cur_col--;
    else goto exit_function;
  } 
  while(c_r->line->str_data[TT.cur_col] > ' ') {
    if (TT.cur_col)TT.cur_col--;
    else goto exit_function;
  }
  TT.cur_col++;
exit_function:
  count--;
  if (count>1)
    return vi_movb(count);
      check_cursor_bounds();
      adjust_screen_buffer();
  return 1;
}

int vi_move(int count) 
{
  if (!c_r)
    return 0;
  if (TT.cur_col < c_r->line->str_len)
    TT.cur_col++;
  if (c_r->line->str_data[TT.cur_col] <= ' ' || count > 1)
    vi_movw(count); //find next word; 
  while(c_r->line->str_data[TT.cur_col] > ' ') 
    TT.cur_col++;
  if (TT.cur_col) TT.cur_col--;
      check_cursor_bounds();
      adjust_screen_buffer();
  return 1;
}

void i_insert()
{
  char* t = xzalloc(c_r->line->alloc_len);
  char* s = c_r->line->str_data;
   int sel = c_r->line->str_len-TT.cur_col;
  strncpy(t,&s[TT.cur_col],sel);
  t[sel+1] = 0;
  if (c_r->line->alloc_len< c_r->line->str_len+il->str_len+5) {
    c_r->line->str_data = xrealloc(c_r->line->str_data,c_r->line->alloc_len*2+il->alloc_len*2);
    c_r->line->alloc_len = c_r->line->alloc_len*2 + 2*il->alloc_len;
    memset(&c_r->line->str_data[c_r->line->str_len],0,c_r->line->alloc_len-c_r->line->str_len);
  s = c_r->line->str_data;
  }
  strcpy(&s[TT.cur_col],il->str_data);
  strcpy(&s[TT.cur_col+il->str_len],t);
  TT.cur_col += il->str_len;
  if (TT.cur_col) TT.cur_col--;
  c_r->line->str_len+=il->str_len;
  free(t);

}
//new line at split pos;
void i_split()
{
  struct str_line* l = xmalloc(sizeof(struct str_line));
  int l_a = c_r->line->alloc_len;
  int l_len = c_r->line->str_len-TT.cur_col;
  l->str_data = xzalloc(l_a);
  l->alloc_len=l_a;
  l->str_len = l_len;
  strncpy(l->str_data,&c_r->line->str_data[TT.cur_col],l_len);
  l->str_data[l_len] = 0;
  c_r->line->str_len-=l_len;
  c_r->line->str_data[c_r->line->str_len] = 0;
  c_r = (struct linelist*)dlist_insert((struct double_list**)&c_r,(char*)l);
  c_r->line = l;
  TT.cur_col=0;
  check_cursor_bounds();
  adjust_screen_buffer();
}

struct vi_cmd_param {
  const char* cmd;
  int (*vi_cmd_ptr)(int);
};
struct vi_cmd_param vi_cmds[7] =
{
  {"dd",&ex_dd},
  {"dw",&ex_dw},
  {"d$",&ex_deol},
  {"w",&vi_movw},
  {"b",&vi_movb},
  {"e",&vi_move},
  {"x",&vi_x},
};
int run_vi_cmd(char* cmd) 
{
  int val = 0;
  char* cmd_e;
  errno = 0;
  val = strtol(cmd, &cmd_e, 10);
  if (errno || val == 0) {
    val = 1;
  }
  else {
    cmd = cmd_e;
  }
  for(int i=0;i<7;i++) {
    if (strstr(cmd,vi_cmds[i].cmd)) {
      return vi_cmds[i].vi_cmd_ptr(val);
    }
  }
  return 0;

}

int search_str(char* s)
{
  struct linelist* lst = c_r;
  char *c = strstr(&c_r->line->str_data[TT.cur_col],s);
  if (c) {
    TT.cur_col = c_r->line->str_data-c;
  TT.cur_col=c-c_r->line->str_data;
  }
  else for(;!c;) {
    lst = lst->down;
    if (!lst) return 1;
    c = strstr(&lst->line->str_data[TT.cur_col],s);
  }
  c_r=lst;
  TT.cur_col=c-c_r->line->str_data;
  return 0;
}
 
int run_ex_cmd(char* cmd)
{
  if (cmd[0] == '/') {
    //search pattern 
    if (!search_str(&cmd[1]) ) {
      check_cursor_bounds();
      adjust_screen_buffer();
    }
  } else if (cmd[0] == '?') {

  } else if (cmd[0] == ':') {
    if (strstr(&cmd[1],"q!")) {
      //exit_application;
      return -1;
    }
    else if (strstr(&cmd[1],"wq")) {
      write_file(0);
      return -1;
    }
    else if (strstr(&cmd[1],"w")) {
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
  int key = 0;
  char vi_buf[16];
  int vi_buf_pos=0;
  il = xzalloc(sizeof(struct str_line));
  il->str_data = xzalloc(80);
  il->alloc_len = 80;
  keybuf[0] = 0;
  memset(vi_buf,0,16);
  memset(utf8_code,0,8);
  linelist_load(0);
  scr_r = text;
  c_r = text;
  TT.cur_row = 0;
  TT.cur_col = 0;
  TT.screen_width = 80;
  TT.screen_height = 24;
  TT.vi_mode = 1;
  terminal_size(&TT.screen_width, &TT.screen_height);
  TT.screen_height -=2; //TODO this is hack fix visual alignment
  set_terminal(0,1,0,0);
  //writes stdout into different xterm buffer so when we exit
  //we dont get scroll log full of junk
  tty_esc("?1049h");
  tty_esc("H");
  xflush();
  draw_page();
  while(1) {
    key = scan_key(keybuf,-1);
    printf("key %d\n",key);
    switch (key) {
      case -1:
      case 3:
      case 4:
        goto cleanup_vi;
    }
    if (TT.vi_mode == 1) { //NORMAL
      switch (key) {
        case 'h':
          cur_left();
          break;
        case 'j':
          cur_down();
          break;
        case 'k':
          cur_up();
          break;
        case 'l':
          cur_right();
          break;
        case '/':
        case '?':
        case ':':
          TT.vi_mode = 0;
          il->str_data[0]=key;
          il->str_len++;
          break;
        case 'a':
          TT.cur_col++;
        case 'i':
          TT.vi_mode = 2;
          break;
        case 27:
          vi_buf[0] = 0;
          vi_buf_pos = 0;
          break;
        default:
          if (key > 0x20 && key < 0x7B) {
            vi_buf[vi_buf_pos] = key;
            vi_buf_pos++;
            if (run_vi_cmd(vi_buf)) {
              memset(vi_buf,0,16);
              vi_buf_pos=0;
            }
            else if (vi_buf_pos==16) {
              vi_buf_pos = 0;
            }

          } 

          break;
      }
    } else if (TT.vi_mode == 0) { //EX MODE
      switch (key) {
        case 27:
          TT.vi_mode=1;
          il->str_len = 0;
          memset(il->str_data,0,il->alloc_len);
          break;
        case 0x7F:
        case 0x08:
          if (il->str_len){
            il->str_data[il->str_len] = 0;
            if (il->str_len>1) il->str_len--;
          }
          break;
        case 0x0D:
            if (run_ex_cmd(il->str_data) == -1)
              goto cleanup_vi;
          TT.vi_mode=1;
          il->str_len = 0;
          memset(il->str_data,0,il->alloc_len);
          break;
        default: //add chars to ex command until ENTER
          if (key >= 0x20 && key < 0x7F) { //might be utf?
            if (il->str_len == il->alloc_len)
            {
              il->str_data = realloc(il->str_data,il->alloc_len*2);
              il->alloc_len *=2;
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
          TT.vi_mode=1;
          il->str_len = 0;
          memset(il->str_data,0,il->alloc_len);
          break;
        case 0x7F:
        case 0x08:
          if (il->str_len)
            il->str_data[il->str_len--] = 0;
          break;
        case 0x09:
          //TODO implement real tabs
          il->str_data[il->str_len++] = ' ';
          il->str_data[il->str_len++] = ' ';
          break;

        case 0x0D:
          //insert newline
          //
          i_insert();
          il->str_len = 0;
          memset(il->str_data,0,il->alloc_len);
          i_split();
          break;
        default:
          if (key >= 0x20 /*&& key < 0x7F) { 
            if (il->str_len == il->alloc_len)
            {
              il->str_data = realloc(il->str_data,il->alloc_len*2);
              il->alloc_len *=2;
            }
            il->str_data[il->str_len] = key;
            il->str_len++;
          } else if (key > 0x7F */&& utf8_dec(key, utf8_code, &utf8_dec_p)) {
            if (il->str_len+utf8_dec_p+1 >= il->alloc_len)
            {
              il->str_data = realloc(il->str_data,il->alloc_len*2);
              il->alloc_len *=2;
            }
            strcpy(il->str_data+il->str_len,utf8_code);
            il->str_len +=utf8_dec_p;
            utf8_dec_p=0;
            *utf8_code=0;

          }
          break;
      }
    }

    draw_page();

  }
cleanup_vi:
 tty_reset();
  tty_esc("?1049l");
}

static void draw_page()
{
  unsigned y = 0;
  int cy_scr =0;
  int cx_scr =0;
  struct linelist* scr_buf= scr_r;
  //clear screen
  tty_esc("2J");
  tty_esc("H");


  tty_jump(0,0);
  for(; y < TT.screen_height; ) {
    if (scr_buf && scr_buf->line->str_data && scr_buf->line->str_len) {
      for(int p = 0; p < scr_buf->line->str_len;y++) {
        unsigned x = 0;
        for(;x<TT.screen_width;x++) {
          if (p < scr_buf->line->str_len) {
            int hi = 0;
            if (scr_buf == c_r && p == TT.cur_col) {
              if (TT.vi_mode == 2) {
                tty_jump(x,y);
                
                tty_esc("1m"); //bold
                printf("%s",il->str_data);
                x+=il->str_len;
                tty_esc("0m"); 
              } 
              cy_scr = y;
              cx_scr = x;
            }
            int l = draw_rune(&scr_buf->line->str_data[p],x,y,hi);
            if (!l)
              break;
            p+=l;
            if (l>2) x++;//traditional chinese is somehow 2 width in tty???
          }
          else {
            if (scr_buf == c_r && p == TT.cur_col) {
              if (TT.vi_mode == 2) {
                tty_jump(x,y);
                
                tty_esc("1m"); //bold
                printf("%s",il->str_data);
                x+=il->str_len;
                tty_esc("0m"); 
              } 
              cy_scr = y;
              cx_scr = x;
            }
            break; 
          }
        }
        printf("\r\n"); 
      }
    }
    else {
      if (scr_buf == c_r){
              cy_scr = y;
              cx_scr = 0;
              if (TT.vi_mode == 2) {
                tty_jump(0,y);
                tty_esc("1m"); //bold
                printf("%s",il->str_data);
                cx_scr +=il->str_len;
                tty_esc("0m"); 
              } else draw_char(' ',0,y,1);
      }
     y++; 
    }
    printf("\n");
    if (scr_buf->down)
      scr_buf=scr_buf->down;
    else break;
  }
  for(;y < TT.screen_height;y++) {
              printf("\n");
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
  tty_esc("47m"); 
  tty_esc("30m"); 
  int i = utf8_len(&c_r->line->str_data[TT.cur_col]);
  if (i) {
    char t[5] = {0,0,0,0,0};
    strncpy(t,&c_r->line->str_data[TT.cur_col],i);
    printf("utf: %d %s",i,t);
  }
  printf("| %d, %d\n",cx_scr,cy_scr); //screen coord
  
  tty_jump(TT.screen_width-12, TT.screen_height);
  printf("| %d, %d\n",TT.cur_row,TT.cur_col);
  tty_esc("37m"); 
  tty_esc("40m");
  if (!TT.vi_mode) {
    tty_esc("1m");
    tty_jump(0,TT.screen_height+1);
    printf("%s",il->str_data);
  } else tty_jump(cx_scr,cy_scr);
  xflush();

}
static void draw_char(char c, int x, int y, int highlight) 
{
  tty_jump(x,y);
  if (highlight) {
    tty_esc("30m"); //foreground black
    tty_esc("47m"); //background white 
  }
  printf("%c",c);
}
//utf rune draw
//printf and useless copy could be replaced by direct write() to stdout
static int draw_rune(char* c,int x,int y, int highlight)
{
  int l = utf8_len(c);
  char t[5] = {0,0,0,0,0};
  if (!l) return 0;
  tty_jump(x,y);
  tty_esc("0m");
  if (highlight) {
    tty_esc("30m"); //foreground black
    tty_esc("47m"); //background white 
  }
  strncpy(t,c,5);
 printf("%s",t);
  tty_esc("0m");
  return l;
}

static void check_cursor_bounds()
{
  if (c_r->line->str_len-1 < TT.cur_col) {
    if (c_r->line->str_len == 0)
      TT.cur_col = 0;
    else
      TT.cur_col = c_r->line->str_len-1;
  }
}

static void adjust_screen_buffer() 
{
  //search cursor and screen TODO move this perhaps
  struct linelist* t = text;
  int c = -1;
  int s = -1;
  int i = 0;
  for(;;) {
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
    int distance = c - s +1;
    //TODO instead iterate scr_r up and check strlen%screen_width 
    //for each iteration
    if (distance >= (int)TT.screen_height) {
      int adj = distance - TT.screen_height;
      while(adj--) {
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
//3 1110xxxx	10xxxxxx	10xxxxxx
//4	11110xxx	10xxxxxx	10xxxxxx	10xxxxxx
static int utf8_len(char* str)
{
  int len=0;
  uint8_t *c = (uint8_t*)str; 
  if (!c || !(*c)) return 0; 
  if (*c < 0x7F) return 1; 
  if ((*c & 0xE0) == 0xc0) len = 2;
  else if ((*c & 0xF0) == 0xE0 ) len = 3;
  else if ((*c & 0xF8) == 0xF0 ) len = 4;
  else return 0;
  c++;
  for(int i = len-1;i>0;i--) {
    if ((*c++ & 0xc0)!=0x80) return 0;
  }
  return len;
}

static int utf8_dec(char key, char* utf8_scratch,int* sta_p) 
{
  int len = 0;
  char* c = utf8_scratch;
  c[*sta_p] = key;
  if (!(*sta_p))  *c = key;
  if (*c < 0x7F) { *sta_p = 1; return 1; }
  if ((*c & 0xE0) == 0xc0) len = 2;
  else if ((*c & 0xF0) == 0xE0 ) len = 3;
  else if ((*c & 0xF8) == 0xF0 ) len = 4;
  else {*sta_p = 0; return 0; }
  
  (*sta_p)++;

  if (*sta_p == 1) return 0;
  if ((c[*sta_p-1] & 0xc0)!=0x80) {*sta_p = 0; return 0; }

  if (*sta_p == len) { c[(*sta_p)] = 0; return 1; }
  
  return 0;
}

static void cur_left()
{
  if (!TT.cur_col) return;
  TT.cur_col--;

  if (!utf8_len(&c_r->line->str_data[TT.cur_col])) cur_left();
}

static void cur_right()
{
  if (TT.cur_col == c_r->line->str_len-1) return;
  TT.cur_col++;
  if (!utf8_len(&c_r->line->str_data[TT.cur_col])) cur_right();
}

static void cur_up()
{
  if (c_r->up != 0)
    c_r = c_r->up;
  if (!utf8_len(&c_r->line->str_data[TT.cur_col])) cur_left();
  check_cursor_bounds();
  adjust_screen_buffer();
}

static void cur_down()
{
  if (c_r->down != 0)
    c_r = c_r->down;
  if (!utf8_len(&c_r->line->str_data[TT.cur_col])) cur_left();
  check_cursor_bounds();
  adjust_screen_buffer();
}
