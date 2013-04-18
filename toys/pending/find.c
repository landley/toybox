/* vi: set sw=4 ts=4:
 *
 * find.c - find files matching a criteria
 *
 * Copyright 2012 Tim Bird <tbird20d@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/find.html

USE_FIND(NEWTOY(find, "?", TOYFLAG_USR|TOYFLAG_BIN))

config FIND
  bool "find"
  default n
  help
    usage: find [<dir>] [<options]

    -name <pattern>    match pattern
    -type [fcdl]       match file type
    !, -a, -o          not, and , or
    (, )               group expressions  
    -mtime [-+]n       match file modification time (to within 24 hours)
    \t\t               +=greater (older) than, -=less (younger) than
*/

/* To Do:
 * -exec action
 */ 

#define FOR_find
#include "toys.h"

GLOBALS(
  char *dir;
)

#define SECONDS_PER_DAY (24*60*60)

int have_action;

struct filter_node {
  struct filter_node *next;
  int op;
  union {
    char *name_regex;
    struct {
      char time_op;
      time_t time;
    } t;
    mode_t type;
    struct {
      int arg_path_index;
      char **exec_args;
    } e;
  } data;
};

static struct filter_node *filter_root;

/* filter operation types */
#define OP_UNKNOWN	0
#define OP_OR		2
#define OP_AND		3
#define OP_NOT		4

#define LPAREN		1 
#define RPAREN		5 

#define MAX_OP		5

#define CHECK_NAME	7
#define CHECK_MTIME	8
#define CHECK_TYPE	9

#define ACTION_PRINT	20
#define ACTION_PRINT0	21
#define ACTION_EXEC	22

#define TEST_LT		0
#define TEST_EQ		1
#define TEST_GT		2

/* executes the command for a filter node
 * return the program return value (0=success)
 */
static int do_exec(struct filter_node *filter, struct dirtree *node)
{
  char *path = NULL;
  int plen;
  char **arg_array;
  pid_t pid;
  int status;

  arg_array = filter->data.e.exec_args;
  if (filter->data.e.arg_path_index) {
    path = dirtree_path(node, &plen);
    arg_array[filter->data.e.arg_path_index] = path;
  }

  if (!(pid = fork())) xexec(arg_array);
  free(path);
  waitpid(pid, &status, 0);

  return WEXITSTATUS(status);
}

/* prefix evaluator */
/* returns 0 for failure or 1 for success */
static int evaluate(struct filter_node *filter, struct dirtree *node,
  struct filter_node **fnext)
{
  int result;
  char *path;
  int plen = 0;
  struct stat st_buf;
  time_t node_time;
  int op;
  static int skip = 0;

  /* if no filters, success */
  if (!filter) {
    *fnext = NULL;
    return 1;
  }
  op = filter->op;

  if (op==OP_NOT) return !evaluate(filter->next, node, fnext);

  if (op==OP_OR || op==OP_AND) {
    result = evaluate(filter->next, node, fnext);
    if(!skip) {
      if (op==OP_OR) {
        skip = result;
        result = evaluate(*fnext, node, fnext) || result;
      } else {
        skip = !result;
        result = evaluate(*fnext, node, fnext) && result;
      }
      skip = 0;
    }
    else result = evaluate(*fnext, node, fnext);
    return result;
  }

  // we're down to single operations, mark our position
  *fnext = filter->next;
  if(skip) return 0;

  // TODO: Do a regex comparison
  if (op==CHECK_NAME)
    return !strcmp(filter->data.name_regex, node->name);

  if (op==CHECK_MTIME) {
    // do mtime check
    path = dirtree_path(node, &plen);
    result = stat(path,  &st_buf);
    free(path);
    if (result<0) return 0;

    node_time = st_buf.st_mtime/SECONDS_PER_DAY;
    result = 1;
    switch (filter->data.t.time_op) {
      /* do time compare here */
      case TEST_LT:
        /* FIXTHIS - should result be < or <= ?*/
        if (node_time > filter->data.t.time) result = 0;
        break;
      case TEST_GT:
        if (node_time < filter->data.t.time) result = 0;
        break;
      case TEST_EQ:
        if (node_time != filter->data.t.time) result = 0;
        break;
      default:
        /* how'd I get here? */
        result = 0;
        break;
    }
    return result;

  }
  if (op==CHECK_TYPE) {
    path = dirtree_path(node, &plen);
    result = lstat(path,  &st_buf);
    free(path);
    if (result<0) return 0;
    return (st_buf.st_mode & S_IFMT) == filter->data.type;
  }


  if (op==ACTION_PRINT || op==ACTION_PRINT0) {
    char terminator = (op==ACTION_PRINT)*'\n';

    path = dirtree_path(node, &plen);
    printf("%s%c", path, terminator);
    free(path);
    return 1;
  }

  if (op==ACTION_EXEC) return !do_exec(filter, node);

  error_msg("Ran out of operations in filter list!");
  return 1;
}

static int check_node_callback(struct dirtree *node)
{
  char *path;
  int plen = 0;
  int result;
  struct filter_node *junk;

  /* only recurse on "." at the root */
  /* so, don't recurse on 1) non-root "." and 2) any ".." */

  if (node->name[0] == '.' && 
    ((!node->name[1] && node->parent) ||
    (node->name[1]=='.' && !node->name[2])))
    return 0;

  result = evaluate(filter_root, node, &junk);
  if (result & !have_action) {
    /* default action is just print the path */
    path = dirtree_path(node, &plen);
    printf("%s\n", path);
    free(path);
  }
  return DIRTREE_RECURSE;
}


static void build_filter_list(void)
{
  struct filter_node *node_list, *op_stack, *node, *op_node, *next;
  char *arg, **arg_array;
  int i, j;
  int prevop = 0;

  /* part optargs here and build a filter list in prefix format */

  TT.dir = ".";
  node_list = NULL;
  have_action = 0;
  for(i=0; toys.optargs[i]; i++) {
    node = (struct filter_node *)
      xmalloc(sizeof(struct filter_node));
    node->op = OP_UNKNOWN;
    arg = toys.optargs[i];
    if (!strcmp(arg, "-o")) node->op = OP_OR;
    if (!strcmp(arg, "-a")) node->op = OP_AND;
    if (!strcmp(arg, "!")) node->op = OP_NOT;
    if (!strcmp(arg, "(")) node->op = LPAREN;
    if (!strcmp(arg, ")")) node->op = RPAREN;

    if (!strcmp(arg, "-name")) {
      node->op = CHECK_NAME;
      arg = toys.optargs[++i];
      if (!arg) error_exit("Missing argument to -name");
      node->data.name_regex = arg;
    }

    if (!strcmp(arg, "-mtime")) {
      node->op = CHECK_MTIME;
      arg = toys.optargs[++i];
      if (!arg) error_exit("Missing argument to -mtime");
      switch(arg[0]) {
        case '+':
          node->data.t.time_op=TEST_GT;
          arg++;
          break;
        case '-':
          node->data.t.time_op=TEST_LT;
          arg++;
          break;
        default:
          node->data.t.time_op=TEST_EQ;
          break;
      }
      /* convert to days (very crudely) */
      node->data.t.time = atoi(arg)/SECONDS_PER_DAY;
    }

    if (!strcmp(arg, "-type")) {
      mode_t types[]={S_IFREG,S_IFDIR,S_IFCHR,S_IFBLK,S_IFLNK,S_IFSOCK,S_IFIFO};
      int j;

      node->op = CHECK_TYPE;
      arg = toys.optargs[++i];
      if (!arg) error_exit("Missing argument to -type");
      if (-1 == (j = stridx("fdcblsp", *arg)))
        error_exit("bad type '%s'", arg);
      else node->data.type = types[j];
    }
    if (!strcmp(arg, "-print")) {
      node->op = ACTION_PRINT;
      have_action = 1;
    }
    if (!strcmp(arg, "-print0")) {
      node->op = ACTION_PRINT0;
      have_action = 1;
    }
    if (!strcmp(arg, "-exec")) {
      node->op = ACTION_EXEC;
      have_action = 1;
      arg_array = xmalloc(sizeof(char *));
      arg = toys.optargs[++i];
      for (j = 0; arg && strcmp(arg, ";"); j++) {
        /* new method */
        arg_array = xrealloc(arg_array, sizeof(char *) * (j+2));
        arg_array[j] = arg;
        if (!strcmp(arg, "{}")) node->data.e.arg_path_index = j;

        arg = toys.optargs[++i];
      }
      if (!arg) error_exit("need ';' in exec");
      arg_array[j] = 0;
      node->data.e.exec_args = arg_array;
    }
    if (node->op == OP_UNKNOWN) {
      if (arg[0] == '-') error_exit("bad option '%s'", arg);
      else TT.dir = arg;
    }  else {
      // add OP_AND where necessary
      if (node_list) {
        int o1 = node_list->op, o2 = node->op;
        if ((o1>MAX_OP && o2>MAX_OP) ||
           (o1==RPAREN && o2>=OP_NOT && o2!=RPAREN) ||
           (o1>=OP_NOT && o1!=LPAREN && o2==LPAREN)) {
          struct filter_node *n = (struct filter_node *)
                                  xmalloc(sizeof(struct filter_node));
          n->op = OP_AND;
          n->next = node_list;
          node_list = n;
        }
      }
      /* push node */
      node->next = node_list;;
      node_list = node;
    }

  }

  /* now convert from infix to prefix */
  filter_root = NULL;
  op_stack = NULL;
  node = node_list;
  while( node ) {
    int op = node->op;
    next = node->next;
    if (op==LPAREN || op==RPAREN) {
      free(node);
      node = 0;
    }
    if (op<=MAX_OP) {
      if (prevop > op) {
        /* pop opstack to output */
        op_node = op_stack;
        while (op_node) {
          /* remove from op_stack */
          op_stack = op_node->next;
          /* push to output */
          op_node->next = filter_root;
          filter_root = op_node;
          /* get next node */
          op_node = op_stack;
        }
      }
      if (node) {
        /* push to opstack */
        node->next = op_stack;
        op_stack = node;
      }
      prevop = op*(op!=RPAREN);
    }
    else {
        /* push to output */
        node->next = filter_root;
        filter_root = node;
    }
    node = next;
  }
  /* end of input - push opstack to output */
  /* pop opstack to output till empty */
  op_node = op_stack;
  while (op_node) {
    op_stack = op_node->next;
    op_node->next = filter_root;
    filter_root = op_node;
    op_node = op_stack;
  }
}

void find_main(void)
{
  /* parse filters, if present */
  build_filter_list();

  /* FIXTHIS - parse actions, if present */

  dirtree_read(TT.dir, check_node_callback);
}
