/* cut.c - Cut from a file.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>, Kyungwan Han <asura321@gamil.com>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/cut.html 
 *
USE_CUT(NEWTOY(cut, "b:c:f:d:sn", TOYFLAG_BIN))

config CUT
  bool "cut"
  default y
  help
    usage: cut OPTION... [FILE]...
    Print selected parts of lines from each FILE to standard output.
      -b LIST    select only these bytes from LIST.
      -c LIST    select only these characters from LIST.
      -f LIST    select only these fields.
      -d DELIM  use DELIM instead of TAB for field delimiter.
      -s    do not print lines not containing delimiters.
      -n    don't split multibyte characters (Ignored).
*/
#define FOR_cut
#include "toys.h"

typedef struct _slist {
  int start_position;
  int end_position;
  struct _slist *next;
}SLIST;

GLOBALS(
  char *delim;
  char *flist;
  char *clist;
  char *blist;
  struct _slist *slist_head;
  unsigned nelem;
)

#define BEGINOFLINE  0
#define ENDOFLINE INT_MAX
#define NORANGE -1

static int get_user_options(void);
void (*do_cut)(int);
static void do_fcut(int fd);
static void do_bccut(int fd);
static void free_list(void);

/*
 * add items in the slist.
 */
static void add_to_list(int start, int end)
{
  SLIST *current, *head_ref, *temp1_node;

  head_ref = TT.slist_head;
  temp1_node = (SLIST *)xzalloc(sizeof(SLIST));
  temp1_node->start_position = start;
  temp1_node->end_position = end;
  temp1_node->next = NULL;

  /* Special case for the head end */
  if (head_ref == NULL || (head_ref)->start_position >= start) { 
      temp1_node->next = head_ref; 
      head_ref = temp1_node;
  }  
  else { 
    /* Locate the node before the point of insertion */   
    current = head_ref;   
    while (current->next!=NULL && current->next->start_position < temp1_node->start_position)
        current = current->next;
    temp1_node->next = current->next;   
    current->next = temp1_node;
  }
  TT.slist_head = head_ref;
  return;
}

/*
 * parse list and add to slist.
 */
static void parse_list(char *list)
{
  char *ctoken, *dtoken;
  int  start = 0, end = 0;
  while((ctoken = strsep(&list, ",")) != NULL) {
    if(!ctoken[0]) continue;

    //Get start position.
    dtoken = strsep(&ctoken, "-");
    if(!dtoken[0]) start = BEGINOFLINE;
    else {
      start = get_int_value(dtoken, 0, INT_MAX);
      start = (start?(start-1):start);
    }
    //Get end position.
    if(ctoken == NULL) end = NORANGE; //case e.g. 1,2,3
    else if(!ctoken[0]) end = ENDOFLINE; //case e.g. N-
    else {//case e.g. N-M
      end = get_int_value(ctoken, 0, INT_MAX);
      end = (end?end:ENDOFLINE);
            end--;
      if(end == start) end = NORANGE;
    }
    add_to_list(start, end);
    TT.nelem++;
  }
  //if list is missing in command line.
  if(TT.nelem == 0) error_exit("missing positions list:");
  return;
}

/*
 * retrive data from the file/s.
 */
static void get_data(void)
{
  char **argv = toys.optargs; //file name.
  toys.exitval = EXIT_SUCCESS;

  if(!*argv) do_cut(0); //for stdin
  else {
    for(; *argv; ++argv) {
      if(strcmp(*argv, "-") == 0) do_cut(0); //for stdin
      else {
        int fd = open(*argv, O_RDONLY, 0);
        if(fd < 0) {//if file not present then continue with other files.
          perror_msg(*argv);
          continue;
        }
        do_cut(fd);
        xclose(fd);
      }
    }
  }
  return;
}

/*
 * cut main function.
*/
void cut_main(void)
{
  char delimiter = '\t'; //default delimiter.
  int  num_of_options = 0;
  char *list;

  TT.nelem = 0;
  TT.slist_head = NULL;
  //verify the number of options provided by user.
  num_of_options = get_user_options();
  if(num_of_options == 0) error_exit("specify a list of bytes, characters, or fields");
  else if(num_of_options > 1) error_exit("only one type of list may be specified");

  //Get list and assign the function.
  if(toys.optflags & FLAG_f) {
    list = TT.flist;
    do_cut = do_fcut;
  }
  else if(toys.optflags & FLAG_c) {
    list = TT.clist;
    do_cut = do_bccut;
  }
  else {
    list = TT.blist;
    do_cut = do_bccut;
  }

  if(toys.optflags & FLAG_d) {
    //delimiter must be 1 char.
    if(TT.delim[0] && TT.delim[1]) perror_exit("the delimiter must be a single character");
    delimiter = TT.delim[0];
  }

  if(!(toys.optflags & FLAG_d) && (toys.optflags & FLAG_f)) {
    TT.delim = (char *)xzalloc(2);
    TT.delim[0] = delimiter;
  }
  
  //when field is not specified, cutting has some special handling.
  if(!(toys.optflags & FLAG_f)) {
    if(toys.optflags & FLAG_s) perror_exit("suppressing non-delimited lines operating on fields");
    if(delimiter != '\t') perror_exit("an input delimiter may be specified only when operating on fields");
  }

  parse_list(list);
  get_data();
  if(!(toys.optflags & FLAG_d) && (toys.optflags & FLAG_f)) {
    free(TT.delim);
    TT.delim = NULL;
  }
  free_list();
  return;
}

/*
 * perform cut operation on the given delimiter.
 */
static void do_fcut(int fd)
{
  char *buff;
  char *delimiter = TT.delim;
  while((buff = get_line(fd)) != NULL) {
    //does line have any delimiter?.
    if(strrchr(buff, (int)delimiter[0]) == NULL) {
      //if not then print whole line and move to next line.
      if(!(toys.optflags & FLAG_s)) xputs(buff);
      continue;
    }

    unsigned cpos = 0;
    int start, ndelimiters = -1;
    int  nprinted_fields = 0;
    char *pfield = xzalloc(strlen(buff) + 1);
    SLIST *temp_node = TT.slist_head;

    if(temp_node != NULL) {
      //process list on each line.
      while(cpos < TT.nelem && buff) {
        if(!temp_node) break;
        start = temp_node->start_position;
        do {
          char *field = NULL;
          //count number of delimeters per line.
          while(buff) {
            if(ndelimiters < start) {
              ndelimiters++;
              field = strsep(&buff, delimiter);
            }
            else break;
          }
          //print field (if not yet printed).
          if(!pfield[ndelimiters]) {
            if(ndelimiters == start) {
              //put delimiter.
              if(nprinted_fields++ > 0) xputc(delimiter[0]);
              if(field) fputs(field, stdout);
              //make sure this field won't print again.
              pfield[ndelimiters] = (char) 0x23; //put some char at this position.
            }
          }
          start++;
          if((temp_node->end_position == NORANGE) || (!buff)) break;          
        }while(start <= temp_node->end_position);
        temp_node = temp_node->next;
        cpos++;
      }
    }
    xputc('\n');
    free(pfield);
    pfield = NULL;
  }//End of while loop.
  return;
}

/*
 * perform cut operation char or byte.
 */
static void do_bccut(int fd)
{
  char *buff;
  while((buff = get_line(fd)) != NULL) {
    unsigned cpos = 0;
    int buffln = strlen(buff);
    char *pfield = xzalloc(buffln + 1);
    SLIST *temp_node = TT.slist_head;
    if(temp_node != NULL) {
      while(cpos < TT.nelem) {
        int start;
        if(!temp_node) break;
        start = temp_node->start_position;
        while(start < buffln) {
          //to avoid duplicate field printing.
          if(pfield[start]) {
              if(++start <= temp_node->end_position) continue;
              temp_node = temp_node->next;
              break;
          }
          else {
            //make sure this field won't print again.
            pfield[start] = (char) 0x23; //put some char at this position.
            xputc(buff[start]);
          }
          if(++start > temp_node->end_position) {
            temp_node = temp_node->next;
            break;
          }
        }
        cpos++;
      }
      xputc('\n');
    }
    free(pfield);
    pfield = NULL;
  }
  return;
}

/* 
 * used to verify "c", "b" and "f" options are present in command line.
 * As cut command can only except any of them at a time.
*/
static int get_user_options(void)
{
  int i = 3, flag = 0;
  for(; i<6; i++) {//verify 3rd, 4th and 5th bit is set.
    int mask = 1;
    mask = mask << i;
    if(toys.optflags & mask) flag++;
  }
  return flag;
}

/*
 * free the slist.
*/
static void free_list(void)
{
  SLIST *temp;
  while(TT.slist_head != NULL) {
    temp = TT.slist_head->next;
    free(TT.slist_head);
    TT.slist_head = temp;
  }
  return;
}
