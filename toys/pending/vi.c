/* vi.c - You can't spell "evil" without "vi".
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 * Copyright 2019 Jarno Mäkipää <jmakip87@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/vi.html

USE_VI(NEWTOY(vi, ">1s:", TOYFLAG_USR|TOYFLAG_BIN))

config VI
  bool "vi"
  default n
  help
    usage: vi [-s script] FILE
    -s script: run script file
    Visual text editor. Predates the existence of standardized cursor keys,
    so the controls are weird and historical.
*/

#define FOR_vi
#include "toys.h"

GLOBALS(
    char *s;
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
    struct str_line {
      int alloc;
      int len;
      char *data;
    } *il;
    size_t screen; //offset in slices must be higher than cursor
    size_t cursor; //offset in slices
    //yank buffer
    struct yank_buf {
      char reg;
      int alloc;
      char* data;
    } yank;

// mem_block contains RO data that is either original file as mmap
// or heap allocated inserted data
//
//
//
  struct block_list {
    struct block_list *next, *prev;
    struct mem_block {
      size_t size;
      size_t len;
      enum alloc_flag {
        MMAP,  //can be munmap() before exit()
        HEAP,  //can be free() before exit()
        STACK, //global or stack perhaps toybuf
      } alloc;
      const char *data;
    } *node;
  } *text;

// slices do not contain actual allocated data but slices of data in mem_block
// when file is first opened it has only one slice.
// after inserting data into middle new mem_block is allocated for insert data
// and 3 slices are created, where first and last slice are pointing to original
// mem_block with offsets, and middle slice is pointing to newly allocated block
// When deleting, data is not freed but mem_blocks are sliced more such way that
// deleted data left between 2 slices
  struct slice_list {
    struct slice_list *next, *prev;
    struct slice {
      size_t len;
      const char *data;
    } *node;
  } *slices;

  size_t filesize;
  int fd; //file_handle

)

static const char *blank = " \n\r\t";
static const char *specials = ",.:;=-+*/(){}<>[]!@#$%^&|\\?\"\'";

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


static void draw_page();

//utf8 support
static int utf8_lnw(int* width, char* str, int bytes);
static int utf8_dec(char key, char *utf8_scratch, int *sta_p);
static char* utf8_last(char* str, int size);


static int cur_left(int count0, int count1, char* unused);
static int cur_right(int count0, int count1, char* unused);
static int cur_up(int count0, int count1, char* unused);
static int cur_down(int count0, int count1, char* unused);
static void check_cursor_bounds();
static void adjust_screen_buffer();
static int search_str(char *s);

//from TT.cursor to
static int vi_yank(char reg, size_t from, int flags);
static int vi_delete(char reg, size_t from, int flags);


struct double_list *dlist_add_before(struct double_list **head,
  struct double_list **list, char *data)
{
  struct double_list *new = xmalloc(sizeof(struct double_list));
  new->data = data;
  if (*list == *head) *head = new;

  dlist_add_nomalloc(list, new);
  return new;
}

struct double_list *dlist_add_after(struct double_list **head,
  struct double_list **list, char *data)
{
  struct double_list *new = xmalloc(sizeof(struct double_list));
  new->data = data;

  if (*list) {
    new->prev = *list;
    new->next = (*list)->next;
    (*list)->next->prev = new;
    (*list)->next = new;
  } else *head = *list = new->next = new->prev = new;
  return new;
}

// str must be already allocated
// ownership of allocated data is moved
// data, pre allocated data
// offset, offset in whole text
// size, data allocation size of given data
// len, length of the string
// type, define allocation type for cleanup purposes at app exit
static int insert_str(const char *data, size_t offset, size_t size, size_t len,
  enum alloc_flag type)
{
  struct mem_block *b = xmalloc(sizeof(struct mem_block));
  struct slice *next = xmalloc(sizeof(struct slice));
  struct slice_list *s = TT.slices;
  b->size = size;
  b->len = len;
  b->alloc = type;
  b->data = data;
  next->len = len;
  next->data = data;

  //mem blocks can be just added unordered
  TT.text = (struct block_list *)dlist_add((struct double_list **)&TT.text,
    (char *)b);

  if (!s) {
    TT.slices = (struct slice_list *)dlist_add(
      (struct double_list **)&TT.slices,
      (char *)next);
  } else {
    size_t pos = 0;
    //search insertation point for slice
    do {
      if (pos<=offset && pos+s->node->len>offset) break;
      pos += s->node->len;
      s = s->next;
      if (s == TT.slices) return -1; //error out of bounds
    } while (1);
    //need to cut previous slice into 2 since insert is in middle
    if (pos+s->node->len>offset && pos!=offset) {
      struct slice *tail = xmalloc(sizeof(struct slice));
      tail->len = s->node->len-(offset-pos);
      tail->data = s->node->data+(offset-pos);
      s->node->len = offset-pos;
      //pos = offset;
      s = (struct slice_list *)dlist_add_after(
        (struct double_list **)&TT.slices,
        (struct double_list **)&s,
        (char *)tail);

      s = (struct slice_list *)dlist_add_before(
        (struct double_list **)&TT.slices,
        (struct double_list **)&s,
        (char *)next);
    } else if (pos==offset) {
      // insert before
      s = (struct slice_list *)dlist_add_before(
        (struct double_list **)&TT.slices,
        (struct double_list **)&s,
        (char *)next);
    } else {
      // insert after
      s = (struct slice_list *)dlist_add_after((struct double_list **)&TT.slices,
      (struct double_list **)&s,
      (char *)next);
    }
  }
  return 0;
}

// this will not free any memory
// will only create more slices depending on position
static int cut_str(size_t offset, size_t len)
{
  struct slice_list *e, *s = TT.slices;
  size_t end = offset+len;
  size_t epos, spos = 0;
  if (!s) return -1;

  //find start and end slices
  for (;;) {
    if (spos<=offset && spos+s->node->len>offset) break;
    spos += s->node->len;
    s = s->next;

    if (s == TT.slices) return -1; //error out of bounds
  }

  for (e = s, epos = spos; ; ) {
    if (epos<=end && epos+e->node->len>end) break;
    epos += e->node->len;
    e = e->next;

    if (e == TT.slices) return -1; //error out of bounds
  }

  for (;;) {
    if (spos == offset && ( end >= spos+s->node->len)) {
      //cut full
      spos += s->node->len;
      offset += s->node->len;
      s = dlist_pop(&s);

      if (s == TT.slices) TT.slices = s->next;
    }

    else if (spos < offset && ( end >= spos+s->node->len)) {
      //cut end
      size_t clip = s->node->len - (offset - spos);
      offset = spos+s->node->len;
      spos += s->node->len;
      s->node->len -= clip;
    }

    else if (spos == offset && s == e) {
      //cut begin
      size_t clip = end - offset;
      s->node->len -= clip;
      s->node->data += clip;
      break;
    }

    else {
      //cut middle
      struct slice *tail = xmalloc(sizeof(struct slice));
      size_t clip = end-offset;
      tail->len = s->node->len-(offset-spos)-clip;
      tail->data = s->node->data+(offset-spos)+clip;
      s->node->len = offset-spos; //wrong?
      s = (struct slice_list *)dlist_add_after(
        (struct double_list **)&TT.slices,
        (struct double_list **)&s,
        (char *)tail);
      break;
    }
    if (s == e) break;

    s = s->next;
  }

  return 0;
}

//find offset position in slices
static struct slice_list *slice_offset(size_t *start, size_t offset)
{
  struct slice_list *s = TT.slices;
  size_t spos = 0;

  //find start
  for ( ;s ; ) {
    if (spos<=offset && spos+s->node->len>offset) break;

    spos += s->node->len;
    s = s->next;

    if (s == TT.slices) s = 0; //error out of bounds
  }
  if (s) *start = spos;
  return s;
}

static size_t text_strchr(size_t offset, char c)
{
  struct slice_list *s = TT.slices;
  size_t epos, spos = 0;
  int i = 0;

  //find start
  if (!(s = slice_offset(&spos, offset))) return SIZE_MAX;

  i = offset-spos;
  epos = spos+i;
  do {
    for (; i < s->node->len; i++, epos++)
      if (s->node->data[i] == c) return epos;
    s = s->next;
    i = 0;
  } while (s != TT.slices);

  return SIZE_MAX;
}

static size_t text_strrchr(size_t offset, char c)
{
  struct slice_list *s = TT.slices;
  size_t epos, spos = 0;
  int i = 0;

  //find start
  if (!(s = slice_offset(&spos, offset))) return SIZE_MAX;

  i = offset-spos;
  epos = spos+i;
  do {
    for (; i >= 0; i--, epos--)
      if (s->node->data[i] == c) return epos;
    s = s->prev;
    i = s->node->len-1;
  } while (s != TT.slices->prev); //tail

  return SIZE_MAX;
}

static size_t text_filesize()
{
  struct slice_list *s = TT.slices;
  size_t pos = 0;
  if (s) do {

    pos += s->node->len;
    s = s->next;

  } while (s != TT.slices);

  return pos;
}

static int text_count(size_t start, size_t end, char c)
{
  struct slice_list *s = TT.slices;
  size_t i, count = 0, spos = 0;
  if (!(s = slice_offset(&spos, start))) return 0;
  i = start-spos;
  if (s) do {
    for (; i < s->node->len && spos+i<end; i++)
      if (s->node->data[i] == c) count++;
    if (spos+i>=end) return count;

    spos += s->node->len;
    i = 0;
    s = s->next;

  } while (s != TT.slices);

  return count;
}

static char text_byte(size_t offset)
{
  struct slice_list *s = TT.slices;
  size_t spos = 0;
  //find start
  if (!(s = slice_offset(&spos, offset))) return 0;
  return s->node->data[offset-spos];
}

//utf-8 codepoint -1 if not valid, 0 if out_of_bounds, len if valid
//copies data to dest if dest is not 0
static int text_codepoint(char *dest, size_t offset)
{
  char scratch[8] = {0};
  int state = 0, finished = 0;

  for (;!(finished = utf8_dec(text_byte(offset), scratch, &state)); offset++)
    if (!state) return -1;

  if (!finished && !state) return -1;
  if (dest) memcpy(dest,scratch,8);

  return strlen(scratch);
}

static size_t text_sol(size_t offset)
{
  size_t pos;
  if (!TT.filesize || !offset) return 0;
  else if (TT.filesize <= offset) return TT.filesize-1;
  else if ((pos = text_strrchr(offset-1, '\n')) == SIZE_MAX) return 0;
  else if (pos < offset) return pos+1;
  return offset;
}

static size_t text_eol(size_t offset)
{
  if (!TT.filesize) offset = 1;
  else if (TT.filesize <= offset) return TT.filesize-1;
  else if ((offset = text_strchr(offset, '\n')) == SIZE_MAX)
    return TT.filesize-1;
  return offset;
}

static size_t text_nsol(size_t offset)
{
  offset = text_eol(offset);
  if (text_byte(offset) == '\n') offset++;
  if (offset >= TT.filesize) offset--;
  return offset;
}

static size_t text_psol(size_t offset)
{
  offset = text_sol(offset);
  if (offset) offset--;
  if (offset && text_byte(offset-1) != '\n') offset = text_sol(offset-1);
  return offset;
}

static size_t text_getline(char *dest, size_t offset, size_t max_len)
{
  struct slice_list *s = TT.slices;
  size_t end, spos = 0;
  int i, j = 0;

  if (dest) *dest = 0;

  if (!s) return 0;
  if ((end = text_strchr(offset, '\n')) == SIZE_MAX)
    if ((end = TT.filesize)  > offset+max_len) return 0;

  //find start
  if (!(s = slice_offset(&spos, offset))) return 0;

  i = offset-spos;
  j = end-offset+1;
  if (dest) do {
    for (; i < s->node->len && j; i++, j--, dest++)
      *dest = s->node->data[i];
    s = s->next;
    i = 0;
  } while (s != TT.slices && j);

  if (dest) *dest = 0;

  return end-offset;
}

//copying is needed when file has lot of inserts that are
//just few char long, but not always. Advanced search should
//check big slices directly and just copy edge cases.
//Also this is only line based search multiline
//and regexec should be done instead.
static size_t text_strstr(size_t offset, char *str)
{
  size_t bytes, pos = offset;
  char *s = 0;
  do {
    bytes = text_getline(toybuf, pos, ARRAY_LEN(toybuf));
    if (!bytes) pos++; //empty line
    else if ((s = strstr(toybuf, str))) return pos+(s-toybuf);
    else pos += bytes;
  } while (pos < TT.filesize);

  return SIZE_MAX;
}

static void block_list_free(void *node)
{
  struct block_list *d = node;

  if (d->node->alloc == HEAP) free((void *)d->node->data);
  else if (d->node->alloc == MMAP) munmap((void *)d->node->data, d->node->size);

  free(d->node);
  free(d);
}

static void linelist_unload()
{
  llist_traverse((void *)TT.slices, llist_free_double);
  TT.slices = 0;

  llist_traverse((void *)TT.text, block_list_free);
  TT.text = 0;

  if (TT.fd) {
    xclose(TT.fd);
    TT.fd = 0;
  }
}

static int linelist_load(char *filename)
{
  if (!filename) filename = (char*)*toys.optargs;

  if (filename) {
    int fd;
    size_t len, size;
    char *data;
    if ( (fd = open(filename, O_RDONLY)) <0) return 0;

    size = fdlength(fd);
    if (!(len = lseek(fd, 0, SEEK_END))) len = size;
    lseek(fd, 0, SEEK_SET);

    data = xmmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) return 0;
    insert_str(data, 0, size, len, MMAP);
    TT.filesize = text_filesize();
    TT.fd = fd;
  }

  return 1;
}

static void write_file(char *filename)
{
  struct slice_list *s = TT.slices;
  struct stat st;
  int fd = 0;
  if (!s) return;

  if (!filename) filename = (char*)*toys.optargs;

  sprintf(toybuf, "%s.swp", filename);

  if ( (fd = xopen(toybuf, O_WRONLY | O_CREAT | O_TRUNC)) <0) return;

  do {
    xwrite(fd, (void *)s->node->data, s->node->len );
    s = s->next;
  } while (s != TT.slices);

  linelist_unload();

  xclose(fd);
  if (!stat(filename, &st)) chmod(toybuf, st.st_mode);
  else chmod(toybuf, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  xrename(toybuf, filename);
  linelist_load(filename);

}

static int vi_yy(char reg, int count0, int count1)
{
  size_t history = TT.cursor;
  size_t pos = text_sol(TT.cursor); //go left to first char on line
  TT.vi_mov_flag |= 0x4;

  for (;count0; count0--) TT.cursor = text_nsol(TT.cursor);

  vi_yank(reg, pos, 0);

  TT.cursor = history;
  return 1;
}

static int vi_dd(char reg, int count0, int count1)
{
  size_t pos = text_sol(TT.cursor); //go left to first char on line
  TT.vi_mov_flag |= 0x4;

  for (;count0; count0--) TT.cursor = text_nsol(TT.cursor);

  if (pos == TT.cursor && TT.filesize) pos--;
  vi_delete(reg, pos, 0);
  check_cursor_bounds();
  return 1;
}

static int vi_x(char reg, int count0, int count1)
{
  size_t from = TT.cursor;

  if (text_byte(TT.cursor) == '\n') {
    cur_left(count0-1, 1, 0);
  }
  else {
    cur_right(count0-1, 1, 0);
    if (text_byte(TT.cursor) == '\n') TT.vi_mov_flag |= 2;
    else cur_right(1, 1, 0);
  }

  vi_delete(reg, from, 0);
  check_cursor_bounds();
  return 1;
}

static int vi_movw(int count0, int count1, char* unused)
{
  int count = count0*count1;
  while (count--) {
    char c = text_byte(TT.cursor);
    do {
      if (TT.cursor > TT.filesize-1) break;
      //if at empty jump to non empty
      if (c == '\n') {
        if (++TT.cursor > TT.filesize-1) break;
        if ((c = text_byte(TT.cursor)) == '\n') break;
        continue;
      } else if (strchr(blank, c)) do {
        if (++TT.cursor > TT.filesize-1) break;
        c = text_byte(TT.cursor);
      } while (strchr(blank, c));
      //if at special jump to non special
      else if (strchr(specials, c)) do {
        if (++TT.cursor > TT.filesize-1) break;
        c = text_byte(TT.cursor);
      } while (strchr(specials, c));
      //else jump to empty or spesial
      else do {
        if (++TT.cursor > TT.filesize-1) break;
        c = text_byte(TT.cursor);
      } while (c && !strchr(blank, c) && !strchr(specials, c));

    } while (strchr(blank, c) && c != '\n'); //never stop at empty
  }
  check_cursor_bounds();
  return 1;
}

static int vi_movb(int count0, int count1, char* unused)
{
  int count = count0*count1;
  int type = 0;
  char c;
  while (count--) {
    c = text_byte(TT.cursor);
    do {
      if (!TT.cursor) break;
      //if at empty jump to non empty
      if (strchr(blank, c)) do {
        if (!--TT.cursor) break;
        c = text_byte(TT.cursor);
      } while (strchr(blank, c));
      //if at special jump to non special
      else if (strchr(specials, c)) do {
        if (!--TT.cursor) break;
        type = 0;
        c = text_byte(TT.cursor);
      } while (strchr(specials, c));
      //else jump to empty or spesial
      else do {
        if (!--TT.cursor) break;
        type = 1;
        c = text_byte(TT.cursor);
      } while (!strchr(blank, c) && !strchr(specials, c));

    } while (strchr(blank, c)); //never stop at empty
  }
  //find first
  for (;TT.cursor; TT.cursor--) {
    c = text_byte(TT.cursor-1);
    if (type && !strchr(blank, c) && !strchr(specials, c)) break;
    else if (!type && !strchr(specials, c)) break;
  }

  TT.vi_mov_flag |= 0x80000000;
  check_cursor_bounds();
  return 1;
}

static int vi_move(int count0, int count1, char *unused)
{
  int count = count0*count1;
  int type = 0;
  char c;

  if (count>1) vi_movw(count-1, 1, unused);

  c = text_byte(TT.cursor);
  if (strchr(specials, c)) type = 1;
  TT.cursor++;
  for (;TT.cursor < TT.filesize-1; TT.cursor++) {
    c = text_byte(TT.cursor+1);
    if (!type && (strchr(blank, c) || strchr(specials, c))) break;
    else if (type && !strchr(specials, c)) break;
  }

  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  return 1;
}


static void i_insert(char* str, int len)
{
  if (!str || !len) return;

  insert_str(xstrdup(str), TT.cursor, len, len, HEAP);
  TT.cursor += len;
  TT.filesize = text_filesize();
  TT.vi_mov_flag |= 0x30000000;
}

static int vi_zero(int count0, int count1, char *unused)
{
  TT.cursor = text_sol(TT.cursor);
  TT.cur_col = 0;
  TT.vi_mov_flag |= 0x80000000;
  return 1;
}

static int vi_eol(int count0, int count1, char *unused)
{
  //forward find /n
  TT.cursor = text_strchr(TT.cursor, '\n');
  TT.vi_mov_flag |= 2;
  check_cursor_bounds();
  return 1;
}

//TODO check register where to push from
static int vi_push(char reg, int count0, int count1)
{
  //if row changes during push original cursor position is kept
  //vi inconsistancy
  //if yank ends with \n push is linemode else push in place+1
  size_t history = TT.cursor;
  char *start = TT.yank.data;
  char *eol = strchr(start, '\n');

  if (start[strlen(start)-1] == '\n') {
    if ((TT.cursor = text_strchr(TT.cursor, '\n')) == SIZE_MAX)
      TT.cursor = TT.filesize;
    else TT.cursor = text_nsol(TT.cursor);
  } else cur_right(1, 1, 0);

  i_insert(start, strlen(start));
  if (eol) {
    TT.vi_mov_flag |= 0x10000000;
    TT.cursor = history;
  }

  return 1;
}

static int vi_find_c(int count0, int count1, char *symbol)
{
////  int count = count0*count1;
  size_t pos = text_strchr(TT.cursor, *symbol);
  if (pos != SIZE_MAX) TT.cursor = pos;
  return 1;
}

static int vi_find_cb(int count0, int count1, char *symbol)
{
  //do backward search
  size_t pos = text_strrchr(TT.cursor, *symbol);
  if (pos != SIZE_MAX) TT.cursor = pos;
  return 1;
}

//if count is not spesified should go to last line
static int vi_go(int count0, int count1, char *symbol)
{
  size_t prev_cursor = TT.cursor;
  int count = count0*count1-1;
  TT.cursor = 0;

  if (TT.vi_mov_flag&0x40000000 && (TT.cursor = TT.filesize) > 0)
    TT.cursor = text_sol(TT.cursor-1);
  else if (count) {
    size_t next = 0;
    for ( ;count && (next = text_strchr(next+1, '\n')) != SIZE_MAX; count--)
      TT.cursor = next;
    TT.cursor++;
  }

  check_cursor_bounds();  //adjusts cursor column
  if (prev_cursor > TT.cursor) TT.vi_mov_flag |= 0x80000000;

  return 1;
}

static int vi_delete(char reg, size_t from, int flags)
{
  size_t start = from, end = TT.cursor;

  vi_yank(reg, from, flags);

  if (TT.vi_mov_flag&0x80000000)
    start = TT.cursor, end = from;

  //pre adjust cursor move one right until at next valid rune
  if (TT.vi_mov_flag&2) {
    //int len, width;
    //char *s = end->line->data;
    //len = utf8_lnw(&width, s+col_e, strlen(s+col_e));
    //for (;;) {
      //col_e += len;
      //len = utf8_lnw(&width, s+col_e, strlen(s+col_e));
      //if (len<1 || width || !(*(s+col_e))) break;
    //}
  }
  //find if range contains atleast single /n
  //if so set TT.vi_mov_flag |= 0x10000000;

  //do slice cut
  cut_str(start, end-start);

  //cursor is at start at after delete
  TT.cursor = start;
  TT.filesize = text_filesize();
  //find line start by strrchr(/n) ++
  //set cur_col with crunch_n_str maybe?

  return 1;
}


static int vi_D(char reg, int count0, int count1)
{
  size_t pos = TT.cursor;
  if (!count0) return 1;
  vi_eol(1, 1, 0);
  vi_delete(reg, pos, 0);
  count0--;
  if (count0) {
    vi_dd(reg, count0, 1);
  }
  check_cursor_bounds();
  return 1;
}

static int vi_join(char reg, int count0, int count1)
{
  size_t next;
  while (count0--) {
    //just strchr(/n) and cut_str(pos, 1);
    if ((next = text_strchr(TT.cursor, '\n')) == SIZE_MAX) break;
    TT.cursor = next+1;
    vi_delete(reg, TT.cursor-1, 0);
  }
  return 1;
}

static int vi_find_next(char reg, int count0, int count1)
{
  if (TT.last_search) search_str(TT.last_search);
  return 1;
}

static int vi_change(char reg, size_t to, int flags)
{
  vi_delete(reg, to, flags);
  TT.vi_mode = 2;
  return 1;
}

//TODO search yank buffer by register
//TODO yanks could be separate slices so no need to copy data
//now only supports default register
static int vi_yank(char reg, size_t from, int flags)
{
  size_t start = from, end = TT.cursor;
  char *str;

  memset(TT.yank.data, 0, TT.yank.alloc);
  if (TT.vi_mov_flag&0x80000000) start = TT.cursor, end = from;
  else TT.cursor = start; //yank moves cursor to left pos always?

  if (TT.yank.alloc < end-from) {
    size_t new_bounds = (1+end-from)/1024;
    new_bounds += ((1+end-from)%1024) ? 1 : 0;
    new_bounds *= 1024;
    TT.yank.data = xrealloc(TT.yank.data, new_bounds);
    TT.yank.alloc = new_bounds;
  }

  //this is naive copy
  for (str = TT.yank.data ; start<end; start++, str++) *str = text_byte(start);

  *str = 0;

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
  int (*vi_cmd)(char, size_t, int);//REG,from,FLAGS
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

static int run_vi_cmd(char *cmd)
{
  int i = 0, val = 0;
  char *cmd_e;
  int (*vi_cmd)(char, size_t, int) = 0;
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
    int prev_cursor = TT.cursor;
    if (vi_mov(TT.count0, TT.count1, cmd)) {
      if (vi_cmd) return (vi_cmd(TT.vi_reg, prev_cursor, TT.vi_mov_flag));
      else return 1;
    } else return 0; //return some error
  }
  return 0;
}

static int search_str(char *s)
{
  size_t pos = text_strstr(TT.cursor+1, s);

  if (TT.last_search != s) {
    free(TT.last_search);
    TT.last_search = xstrdup(s);
  }

  if (pos != SIZE_MAX) TT.cursor = pos;
  check_cursor_bounds();
  return 0;
}

static int run_ex_cmd(char *cmd)
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
  FILE *script = 0;
  if (FLAG(s)) script = fopen(TT.s, "r");

  TT.il = xzalloc(sizeof(struct str_line));
  TT.il->data = xzalloc(80);
  TT.yank.data = xzalloc(128);

  TT.il->alloc = 80, TT.yank.alloc = 128;

  linelist_load(0);
  TT.screen = TT.cursor = 0;

  TT.vi_mov_flag = 0x20000000;
  TT.vi_mode = 1, TT.tabstop = 8;
  TT.screen_width = 80, TT.screen_height = 24;

  terminal_size(&TT.screen_width, &TT.screen_height);
  TT.screen_height -= 1;

  set_terminal(0, 1, 0, 0);
  //writes stdout into different xterm buffer so when we exit
  //we dont get scroll log full of junk
  tty_esc("?1049h");
  tty_esc("H");
  xflush(1);


  draw_page();
  for (;;) {
    int key = 0;
    if (script) {
      key = fgetc(script);
      if (key == EOF) {
        fclose(script);
        script = 0;
        key = scan_key(keybuf, -1);
      }
    } else key = scan_key(keybuf, -1);

    if (key == -1) goto cleanup_vi;

    terminal_size(&TT.screen_width, &TT.screen_height);
    TT.screen_height -= 1; //TODO this is hack fix visual alignment

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
          TT.il->data[0]=key;
          TT.il->len++;
          break;
        case 'A':
          vi_eol(1, 1, 0);
          TT.vi_mode = 2;
          break;
        case 'a':
          cur_right(1, 1, 0);
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
          if (TT.il->len > 1) {
            TT.il->data[--TT.il->len] = 0;
            break;
          }
          // FALLTHROUGH
        case 27:
          TT.vi_mode = 1;
          TT.il->len = 0;
          memset(TT.il->data, 0, TT.il->alloc);
          break;
        case 0x0A:
        case 0x0D:
          if (run_ex_cmd(TT.il->data) == -1)
            goto cleanup_vi;
          TT.vi_mode = 1;
          TT.il->len = 0;
          memset(TT.il->data, 0, TT.il->alloc);
          break;
        default: //add chars to ex command until ENTER
          if (key >= 0x20 && key < 0x7F) { //might be utf?
            if (TT.il->len == TT.il->alloc) {
              TT.il->data = realloc(TT.il->data, TT.il->alloc*2);
              TT.il->alloc *= 2;
            }
            TT.il->data[TT.il->len] = key;
            TT.il->len++;
          }
          break;
      }
    } else if (TT.vi_mode == 2) {//INSERT MODE
      switch (key) {
        case 27:
          i_insert(TT.il->data, TT.il->len);
          cur_left(1, 1, 0);
          TT.vi_mode = 1;
          TT.il->len = 0;
          memset(TT.il->data, 0, TT.il->alloc);
          break;
        case 0x7F:
        case 0x08:
          if (TT.il->len) {
            char *last = utf8_last(TT.il->data, TT.il->len);
            int shrink = strlen(last);
            memset(last, 0, shrink);
            TT.il->len -= shrink;
          }
          break;
        case 0x0A:
        case 0x0D:
          //insert newline
          //
          TT.il->data[TT.il->len++] = '\n';
          i_insert(TT.il->data, TT.il->len);
          TT.il->len = 0;
          memset(TT.il->data, 0, TT.il->alloc);
          break;
        default:
          if ((key >= 0x20 || key == 0x09) &&
              utf8_dec(key, utf8_code, &utf8_dec_p)) {

            if (TT.il->len+utf8_dec_p+1 >= TT.il->alloc) {
              TT.il->data = realloc(TT.il->data, TT.il->alloc*2);
              TT.il->alloc *= 2;
            }
            strcpy(TT.il->data+TT.il->len, utf8_code);
            TT.il->len += utf8_dec_p;
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
  free(TT.il->data), free(TT.il), free(TT.yank.data);
  tty_reset();
  tty_esc("?1049l");
}

static int vi_crunch(FILE* out, int cols, int wc)
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
  } else if (wc == '\n') return 0;
  return ret;
}

//crunch_str with n bytes restriction for printing substrings or
//non null terminated strings
static int crunch_nstr(char **str, int width, int n, FILE *out, char *escmore,
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

//BUGBUG cursor at eol
static void draw_page()
{
  unsigned y = 0;
  int x = 0;

  char *line = 0, *end = 0;
  int bytes = 0;

  //screen coordinates for cursor
  int cy_scr = 0, cx_scr = 0;

  //variables used only for cursor handling
  int aw = 0, iw = 0, clip = 0, margin = 8;

  int scroll = 0, redraw = 0;

  int SSOL, SOL;


  adjust_screen_buffer();
  //redraw = 3; //force full redraw
  redraw = (TT.vi_mov_flag & 0x30000000)>>28;

  scroll = TT.drawn_row-TT.scr_row;
  if (TT.drawn_row<0 || TT.cur_row<0 || TT.scr_row<0) redraw = 3;
  else if (abs(scroll)>TT.screen_height/2) redraw = 3;

  tty_jump(0, 0);
  if (redraw&2) tty_esc("2J"), tty_esc("H");   //clear screen
  else if (scroll>0) printf("\033[%dL", scroll);  //scroll up
  else if (scroll<0) printf("\033[%dM", -scroll); //scroll down

  SOL = text_sol(TT.cursor);
  bytes = text_getline(toybuf, SOL, ARRAY_LEN(toybuf));
  line = toybuf;

  for (SSOL = TT.screen, y = 0; SSOL < SOL; y++) SSOL = text_nsol(SSOL);

  cy_scr = y;

  //draw cursor row
  /////////////////////////////////////////////////////////////
  //for long lines line starts to scroll when cursor hits margin
  bytes = TT.cursor-SOL; // TT.cur_col;
  end = line;


  tty_jump(0, y);
  tty_esc("2K");
  //find cursor position
  aw = crunch_nstr(&end, 1024, bytes, 0, "\t\n", vi_crunch);

  //if we need to render text that is not inserted to buffer yet
  if (TT.vi_mode == 2 && TT.il->len) {
    char* iend = TT.il->data; //input end
    x = 0;
    //find insert end position
    iw = crunch_str(&iend, 1024, 0, "\t\n", vi_crunch);
    clip = (aw+iw) - TT.screen_width+margin;

    //if clipped area is bigger than text before insert
    if (clip > aw) {
      clip -= aw;
      iend = TT.il->data;

      iw -= crunch_str(&iend, clip, 0, "\t\n", vi_crunch);
      x = crunch_str(&iend, iw, stdout, "\t\n", vi_crunch);
    } else {
      iend = TT.il->data;
      end = line;

      //if clipped area is substring from cursor row start
      aw -= crunch_nstr(&end, clip, bytes, 0, "\t\n", vi_crunch);
      x = crunch_str(&end, aw,  stdout, "\t\n", vi_crunch);
      x += crunch_str(&iend, iw, stdout, "\t\n", vi_crunch);
    }
  }
  //when not inserting but still need to keep cursor inside screen
  //margin area
  else if ( aw+margin > TT.screen_width) {
    clip = aw-TT.screen_width+margin;
    end = line;
    aw -= crunch_nstr(&end, clip, bytes, 0, "\t\n", vi_crunch);
    x = crunch_str(&end, aw,  stdout, "\t\n", vi_crunch);
  }
  else {
    end = line;
    x = crunch_nstr(&end, aw, bytes, stdout, "\t\n", vi_crunch);
  }
  cx_scr = x;
  cy_scr = y;
  x += crunch_str(&end, TT.screen_width-x,  stdout, "\t\n", vi_crunch);

  //start drawing all other rows that needs update
  ///////////////////////////////////////////////////////////////////
  y = 0, SSOL = TT.screen, line = toybuf;
  bytes = text_getline(toybuf, SSOL, ARRAY_LEN(toybuf));

  //if we moved around in long line might need to redraw everything
  if (clip != TT.drawn_col) redraw = 3;

  for (; y < TT.screen_height; y++ ) {
    int draw_line = 0;
    if (SSOL == SOL) {
      line = toybuf;
      SSOL += bytes+1;
      bytes = text_getline(line, SSOL, ARRAY_LEN(toybuf));
      continue;
    } else if (redraw) draw_line++;
    else if (scroll<0 && TT.screen_height-y-1<-scroll)
      scroll++, draw_line++;
    else if (scroll>0) scroll--, draw_line++;

    tty_jump(0, y);
    if (draw_line) {

      tty_esc("2K");
      if (line) {
        if (draw_line && line && strlen(line)) {

          aw = crunch_nstr(&line, clip, bytes, 0, "\t\n", vi_crunch);
          crunch_str(&line, TT.screen_width-1, stdout, "\t\n", vi_crunch);
          if ( *line ) printf("@");

        }
      } else if (draw_line) printf("~");
    }
    if (SSOL+bytes < TT.filesize)  {
      line = toybuf;
      SSOL += bytes+1;
      bytes = text_getline(line, SSOL, ARRAY_LEN(toybuf));
   } else line = 0;
  }

  TT.drawn_row = TT.scr_row, TT.drawn_col = clip;

  //finished updating visual area
  tty_jump(0, TT.screen_height);
  tty_esc("2K");
  if (TT.vi_mode == 2) printf("\x1b[1m-- INSERT --\x1b[m");
  if (!TT.vi_mode) printf("\x1b[1m%s \x1b[m",TT.il->data);

  sprintf(toybuf, "%zu / %zu,%d,%d", TT.cursor, TT.filesize,
    TT.cur_row+1, TT.cur_col+1);

  if (TT.cur_col != cx_scr) sprintf(toybuf+strlen(toybuf),"-%d", cx_scr+1);

  tty_jump(TT.screen_width-strlen(toybuf), TT.screen_height);
  printf("%s", toybuf);

  if (TT.vi_mode) tty_jump(cx_scr, cy_scr);

  xflush(1);

}
//jump into valid offset index
//and valid utf8 codepoint
static void check_cursor_bounds()
{
  char buf[8] = {0};
  int len, width = 0;
  if (!TT.filesize) TT.cursor = 0;

  for (;;) {
    if (TT.cursor < 1) {
      TT.cursor = 0;
      return;
    } else if (TT.cursor >= TT.filesize-1) {
      TT.cursor = TT.filesize-1;
      return;
    }
    if ((len = text_codepoint(buf, TT.cursor)) < 1) {
      TT.cursor--; //we are not in valid data try jump over
      continue;
    }
    if (utf8_lnw(&width, buf, len) && width) break;
    else TT.cursor--; //combine char jump over
  }
}

//TODO rewrite the logic, difficulties counting lines
//and with big files scroll should not rely in knowing
//absoluteline numbers
static void adjust_screen_buffer()
{
  size_t c, s;
  TT.cur_row = 0, TT.scr_row = 0;
  if (!TT.cursor) {
    TT.screen = 0;
    TT.vi_mov_flag = 0x20000000;
    return;
  } else if (TT.screen > (1<<18) || TT.cursor > (1<<18)) {
     //give up, file is big, do full redraw

    TT.screen = text_strrchr(TT.cursor-1, '\n')+1;
    TT.vi_mov_flag = 0x20000000;
    return;
  }

  s = text_count(0, TT.screen, '\n');
  c = text_count(0, TT.cursor, '\n');
  if (s >= c) {
    TT.screen = text_strrchr(TT.cursor-1, '\n')+1;
    s = c;
    TT.vi_mov_flag = 0x20000000; //TODO I disabled scroll
  } else {
    int distance = c-s+1;
    if (distance > (int)TT.screen_height) {
      int n, adj = distance-TT.screen_height;
      TT.vi_mov_flag = 0x20000000; //TODO I disabled scroll
      for (;adj; adj--, s++)
        if ((n = text_strchr(TT.screen, '\n'))+1 > TT.screen)
          TT.screen = n+1;
    }
  }

  TT.scr_row = s;
  TT.cur_row = c;

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
    if (!TT.cursor) return 1;

    TT.cursor--;
    check_cursor_bounds();
  }
  return 1;
}

static int cur_right(int count0, int count1, char* unused)
{
  int count = count0*count1;
  char buf[8] = {0};
  int len, width = 0;
  for (;count; count--) {
    if ((len = text_codepoint(buf, TT.cursor)) > 0) TT.cursor += len;
    else TT.cursor++;

    for (;TT.cursor < TT.filesize;) {
      if ((len = text_codepoint(buf, TT.cursor)) < 1) {
        TT.cursor++; //we are not in valid data try jump over
        continue;
      }

      if (utf8_lnw(&width, buf, len) && width) break;
      else TT.cursor += len;
    }
    if (*buf == '\n') break;
  }
  check_cursor_bounds();
  return 1;
}

//TODO column shift
static int cur_up(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count--;) TT.cursor = text_psol(TT.cursor);

  TT.vi_mov_flag |= 0x80000000;
  check_cursor_bounds();
  return 1;
}

//TODO column shift
static int cur_down(int count0, int count1, char* unused)
{
  int count = count0*count1;
  for (;count--;) TT.cursor = text_nsol(TT.cursor);

  check_cursor_bounds();
  return 1;
}
