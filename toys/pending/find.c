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

#include "toys.h"
#include <strings.h>
#include <time.h>

/* to remove debugging statements, uncomment the #define and
 * comment out the next line */
//#define debug 0
int debug = 0;

#define DPRINTF(fmt, args...)	if (debug) printf("DEBUG: " fmt, ##args)

#define SECONDS_PER_DAY (24*60*60)

#define SUCCESS	1

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

char exec_buf[1024];

static struct filter_node *filter_root;

/* filter operation types */
#define OP_UNKNOWN	0
#define OP_NOT		1	
#define OP_OR		2	
#define OP_AND		3	

#define LPAREN		4 
#define RPAREN		5 

#define CHECK_NAME	7
#define CHECK_MTIME	8
#define CHECK_TYPE	9

#define ACTION_PRINT	20
#define ACTION_PRINT0	21
#define ACTION_EXEC	22

#define TEST_LT		0
#define TEST_EQ		1
#define TEST_GT		2

void dump_node(struct filter_node *node)
{
	char c, **arg_array;
	int i;

	switch(node->op) {
		case CHECK_NAME:
			printf("-name '%s' ", node->data.name_regex);
			break;
		case CHECK_MTIME:
			printf("-mtime %d '%s' ", node->data.t.time_op,
				ctime(&node->data.t.time));
			break;
		case CHECK_TYPE:
			switch(node->data.type) {
				case S_IFREG: c='f';
					break;
				case S_IFDIR: c='d';
					break;
				case S_IFCHR: c='c';
					break;
				case S_IFBLK: c='b';
					break;
				case S_IFLNK: c='l';
					break;
				case S_IFSOCK: c='s';
					break;
				case S_IFIFO: c='p';
					break;
				default: c='u';
					break;
			}
			
			printf("-type %c ", c);
			break;
		case OP_NOT:
			printf("! ");
			break;
		case OP_OR:
			printf("-o ");
			break;
		case OP_AND:
			printf("-a ");
			break;
		case LPAREN:
			printf("( ");
			break;
		case RPAREN:
			printf(") ");
			break;
		case ACTION_PRINT:
			printf("-print ");
			break;
		case ACTION_PRINT0:
			printf("-print0 ");
			break;
		case ACTION_EXEC:
			printf("-exec ");
			arg_array=node->data.e.exec_args;
			for(i=0; arg_array[i]; i++) {
				printf("%s ", arg_array[i]);
			}
			printf("; ");
			break;
		default:
			printf("unknown node type (%d) ", node->op);
			break;
	}
}

void dump_filters(struct filter_node *node)
{
	if (!node) {
		printf("no filters\n");
		return;
	}
	do {
		dump_node(node);
		node = node->next;
	} while(node);
	printf("\n");
}

/* executes the command for a filter node
 * return the program return value (0=success)
 */
int do_exec(struct filter_node *filter, struct dirtree *node)
{
	char *path;
	int plen;
	char **arg_array;
	int i;
	pid_t pid;
	int ccode;
	int status;

	arg_array = filter->data.e.exec_args;
	if (filter->data.e.arg_path_index) {
		path = dirtree_path(node, &plen);
		arg_array[filter->data.e.arg_path_index] = path;
	} else {
		path = NULL;
	}
	DPRINTF("Try to execute: '");
	for(i=0; arg_array[i]; i++) {
		DPRINTF("%s ", arg_array[i]);
	}
	DPRINTF("' here!\n");
	
	pid = fork();
	if (pid==0) {
		/* child */
		ccode = execvp(arg_array[0], arg_array);
		if (ccode<0) {
			printf("Error: problem executing sub-program %s\n", arg_array[0]);
			exit(ccode);
		}
	} else {
		/* parent */
		/* wait for child */
		waitpid(pid, &status, 0);
		ccode = WEXITSTATUS(status);
	}
	free(path);
	DPRINTF("do_exec() returning %d\n", ccode);
	return ccode;
}

/* prefix evaluator */
/* returns 0 for failure or SUCCESS for success */
int evaluate(struct filter_node *filter, struct dirtree *node,
	struct filter_node **fnext)
{
	int result, result2;
	char *path;
	int plen = 0;
	struct stat st_buf;
	time_t node_time;
	char terminator;

	/* if no filters, success */
	if (!filter) {
		*fnext = NULL;
		return SUCCESS;
	}

	if (debug) {
		/* show filter node */
		DPRINTF("eval:");
		dump_node(filter);
		DPRINTF("\n");
	}

	if (filter->op==OP_NOT) {
		result = evaluate(filter->next, node, fnext);
		if (result==0) {
			return SUCCESS;
		} else {
			return 0;
		}
	}
	if (filter->op==OP_OR) {
		result = evaluate(filter->next, node, fnext);
		result2 = evaluate(*fnext, node, fnext);
		if (result) {
			return result;
		} else {
			if (result2) {
				return result2;
			} else {
				return 0;
			}
		}
	}
	if (filter->op==OP_AND) {
		result = evaluate(filter->next, node, fnext);
		if (result) {
			result2 = evaluate(*fnext, node, fnext);
			if (result && result2) {
				return SUCCESS;
			}
		}
		return 0;
	}
	/* we're down to single operations, mark our position */
	*fnext = filter->next;
	if (filter->op==CHECK_NAME) {
		//if (regex_cmp(fn->name_regex, node->name)==0)
		if (strcmp(filter->data.name_regex, node->name)==0)
			return SUCCESS;
		else
			return 0;
	}
	if (filter->op==CHECK_MTIME) {
		/* do mtime check */
		path = dirtree_path(node, &plen);
		result = stat(path,  &st_buf);
		free(path);
		if (result<0) {
			return 0;
		}
		node_time = st_buf.st_mtime/SECONDS_PER_DAY;
		result = SUCCESS;
		switch (filter->data.t.time_op) {
			/* do time compare here */
			case TEST_LT:
				/* FIXTHIS - should result be < or <= ?*/
				if (node_time > filter->data.t.time)
					result = 0;
				break;
			case TEST_GT:
				if (node_time < filter->data.t.time)
					result = 0;
				break;
			case TEST_EQ:
				if (node_time != filter->data.t.time)
					result = 0;
				break;
			default:
				/* how'd I get here? */
				result = 0;
				break;
		}
		return result;

	}
	if (filter->op==CHECK_TYPE) {
		path = dirtree_path(node, &plen);
		result = lstat(path,  &st_buf);
		free(path);
		if (result<0) {
			return 0;
		}
		if ((st_buf.st_mode & S_IFMT) == filter->data.type) {
			return SUCCESS;
		} else {
			return 0;
		}
	}
	
	
	if (filter->op==ACTION_PRINT || filter->op==ACTION_PRINT0) {
		if (filter->op==ACTION_PRINT) {
			terminator = '\n';
		} else {
			terminator = 0;
		}
		path = dirtree_path(node, &plen);
		printf("%s%c", path, terminator);
		free(path);
		return SUCCESS;
	}

	if (filter->op==ACTION_EXEC) {
		if (do_exec(filter, node)==0) {
			return SUCCESS;
		} else {
			return 0;
		}
	}
	printf("ERROR: ran out of operations in filter list!!\n");
	return SUCCESS;
}

int check_node_callback(struct dirtree *node)
{
	char *path;
	int plen = 0;
	int result;
	struct filter_node *junk;

	/* only recurse on "." at the root */
	/* so, don't recurse on 1) non-root "." and 2) any ".." */
	//printf("node->name = %s\n", node->name);
	DPRINTF("node->name=%s\n", node->name);
	if (node->name[0] == '.' && 
		((!node->name[1] && node->parent) ||
		(node->name[1]=='.' && !node->name[2])))
		return 0;

	DPRINTF("passed . and .. check, evaluating filters...\n");
	result = evaluate(filter_root, node, &junk);
	DPRINTF("filter evaluation result=%d\n", result);
	if (result & SUCCESS & !have_action) {
		/* default action is just print the path */
		path = dirtree_path(node, &plen);
		printf("%s\n", path);
		free(path);
	}
	return DIRTREE_RECURSE;
}


void build_test_filter(void)
{
	struct filter_node *node;
	int test=3;
	
	if (test==1) { /* test -name */
		printf("using test filter: '-name 'tail.c''\n");
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		node->next = NULL;
		node->op = CHECK_NAME;
		node->data.name_regex = "tail.c";
		filter_root = node;
	}
	if (test==2) { /* test 'not' */
		printf("using test filter: '! -name 'tail.c''\n");
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		node->next = NULL;
		node->op = CHECK_NAME;
		node->data.name_regex = "tail.c";
		filter_root = node;
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		/* put a not on the stack before the check_name */
		node->op = OP_NOT;
		/* push this node */
		node->next = filter_root;
		filter_root = node;
	}
	if (test==3) { /* test 'or' */
		printf("using test filter: '-name 'tail.c' -o -name 'other.c''\n");
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		node->next = NULL;
		node->op = CHECK_NAME;
		node->data.name_regex = "other.c";
		filter_root = node;
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		node->next = filter_root;
		node->op = CHECK_NAME;
		node->data.name_regex = "tail.c";
		filter_root = node;
		node = (struct filter_node *)
				xmalloc(sizeof(struct filter_node));
		/* put an OR on the stack before the check_names */
		node->op = OP_OR;
		/* push this node */
		node->next = filter_root;
		filter_root = node;
	}
}

void build_filter_list(void)
{
	struct filter_node *node_list;
	struct filter_node *op_stack;
	struct filter_node *node, *op_node;
	struct filter_node *next;

	char *arg, **arg_array;
	int i, j;

	/* part optargs here and build a filter list in prefix format */
	
	/* DEBUG - print optargs */
	if (debug) {
		for(i=0; toys.optargs[i]; i++) {
			printf("optargs[%d]=%s\n", i, toys.optargs[i]);
		}
	}

	node_list = NULL;
	toybuf[0] = 0;
	have_action = 0;
	for(i=0; toys.optargs[i]; i++) {
		node = (struct filter_node *)
			xmalloc(sizeof(struct filter_node));
		node->op = OP_UNKNOWN;
		arg = toys.optargs[i];
		if (strcmp(arg, "-o") == 0) { 
			node->op = OP_OR;
		}
		if (strcmp(arg, "-a") == 0) { 
			node->op = OP_AND;
		}
		if (strcmp(arg, "!")==0) { 
			node->op = OP_NOT;
		}
		if (strcmp(arg, "(") == 0) { 
			node->op = LPAREN;
		}
		if (strcmp(arg, ")") == 0) { 
			node->op = RPAREN;
		}
		if (strcmp(arg, "--debug") == 0) {
			debug = 1;
			continue;
		}

		if (strcmp(arg, "-name") == 0) {
			node->op = CHECK_NAME;
			arg = toys.optargs[i+1];
			if (!arg) {
				printf("Error: missing argument to -name\n");
				exit(1);
			}
			node->data.name_regex = arg;
			i++;
		}

		if (strcmp(arg, "-mtime") == 0) {
			node->op = CHECK_MTIME;
			arg = toys.optargs[i+1];
			if (!arg) {
				printf("Error: missing argument to -mtime\n");
				exit(1);
			}
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
			i++;
		}

		if (strcmp(arg, "-type") == 0) {
			node->op = CHECK_TYPE;
			arg = toys.optargs[i+1];
			if (!arg) {
				printf("Error: missing argument to -type\n");
				exit(1);
			}
			switch(arg[0]) {
				case 'f':
					node->data.type = S_IFREG;
					break;
				case 'd':
					node->data.type = S_IFDIR;
					break;
				case 'c':
					node->data.type = S_IFCHR;
					break;
				case 'b':
					node->data.type = S_IFBLK;
					break;
				case 'l':
					node->data.type = S_IFLNK;
					break;
				case 's':
					node->data.type = S_IFSOCK;
					break;
				case 'p':
					/* named pipe */
					node->data.type = S_IFIFO;
					break;
				default:
					printf("Error: unknown file type '%s'\n", arg);
					exit(1);
			}
			i++;
		}
		if (strcmp(arg, "-print") == 0) {
			node->op = ACTION_PRINT;
			have_action = 1;
		}
		if (strcmp(arg, "-print0") == 0) {
			node->op = ACTION_PRINT0;
			have_action = 1;
		}
		if (strcmp(arg, "-exec") == 0) {
			node->op = ACTION_EXEC;
			have_action = 1;
			exec_buf[0] =  0;
			j = 0;
			arg_array = xmalloc(sizeof(char *)*(j+1));
			i++;
			arg = toys.optargs[i];
			while (arg && (strcmp(arg, ";") != 0)) {
				/* new method */
				arg_array = xrealloc(arg_array, sizeof(char *) * (j+2));
				arg_array[j] = arg;
				if (strcmp(arg, "{}") == 0) {
					node->data.e.arg_path_index = j;
				}
				j++;

				i++;
				arg = toys.optargs[i];
			}
			if (!arg) {
				printf("Error: missing ';' in exec command\n");
				exit(1);
			}
			arg_array[j] = 0;
			node->data.e.exec_args = arg_array;
		}
		if (node->op == OP_UNKNOWN) {
			if( arg[0]=='-') {
				printf("Error: unknown option '%s'\n", arg);
				exit(1);
			} else {
				/* use toybuf as start directory */
				strcpy(toybuf, arg);
			}
		}  else {
			/* push node */
			node->next = node_list;;
			node_list = node;
		}
			
	}
	if (debug) {
		printf("here is the infix node list (reversed):\n");
		dump_filters(node_list);
	}

	/* now convert from infix to prefix */
	filter_root = NULL;
	op_stack = NULL;
	node = node_list;
	while( node ) {
		next = node->next;
		switch( node->op ) {
			case OP_AND:
			case OP_OR:
			case OP_NOT:
			case RPAREN:
				/* push to opstack */
				node->next = op_stack;
				op_stack = node;
				break;
			case LPAREN:
				free(node);
				/* pop opstack to output (up to rparen) */
				op_node = op_stack;
				while (op_node && op_node->op != RPAREN) {
					/* remove from op_stack */
					op_stack = op_node->next;
					/* push to output */
					op_node->next = filter_root;
					filter_root = op_node;
					/* get next node */
					op_node = op_stack;
				}
				/* rparen should be on op_stack */
				if (!op_stack) {
					printf("Error: missing right paren\n");
					exit(1);
				}
				/* remove rparen from op_stack */
				op_stack = op_stack->next;
				free(op_node);
				break;
			default:
				/* push to output */
				node->next = filter_root;
				filter_root = node;
				break;
		}
		node = next;
	}
	/* end of input - push opstack to output */
	/* pop opstack to output till empty */
	op_node = op_stack;
	while (op_node) {
		/*if (op_node->op == RPAREN || op_node->op == LPAREN)  {
			printf("Error: extra paren found\n");
			exit(1);
		}
		*/
		op_stack = op_node->next;
		op_node->next = filter_root;
		filter_root = op_node;
		op_node = op_stack;
	}
}

void find_main(void)
{
	int i;

	/* do a special check for --debug */
	for(i=0; toys.optargs[i]; i++) {
		if (strcmp(toys.optargs[i], "--debug")==0) {
			debug = 1;
			printf("[debug mode on]\n");
			/* shift args down, deleting '--debug' */
			for (;toys.optargs[i]; i++) {
				toys.optargs[i] = toys.optargs[i+1];
			}
		}
	}

	/* parse filters, if present */
	/* also, fill toybuf with the directory to start in, if present */
	build_filter_list();
	if (debug) {
		printf("using prefix filter list:\n");
		dump_filters(filter_root);
	}

	/* DEBUG - override parsed filter list with test filter */
	//build_test_filter();	
	//dump_filters(filter_root);

	/* FIXTHIS - parse actions, if present */

	if (toybuf[0]==0) {
		strcpy(toybuf, ".");
	}
	dirtree_read(toybuf, check_node_callback);
}
