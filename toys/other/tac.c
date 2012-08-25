/* vi: set sw=4 ts=4:
 *
 * tac.c - output lines in reverse order
 *
 * Copyright 2012 Rob Landley <rob@landley.net>

USE_TAC(NEWTOY(tac, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config TAC
	bool "tac"
	default y
	help
	  usage: tac [FILE...]

	  Output lines in reverse order.
*/

#include "toys.h"

void do_tac(int fd, char *name)
{
	struct arg_list *list = NULL;
	char *c;

	// Read in lines
	for (;;) {
		struct arg_list *temp;
		int len;

		if (!(c = get_line(fd))) break;

		len = strlen(c);
		if (len && c[len-1]=='\n') c[len-1] = 0;
		temp = xmalloc(sizeof(struct arg_list));
		temp->next = list;
		temp->arg = c;
		list = temp;
	}

	// Play them back.
	while (list) {
		struct arg_list *temp = list->next;
		xputs(list->arg);
		free(list->arg);
		free(list);
		list = temp;
	}
}

void tac_main(void)
{
	loopfiles(toys.optargs, do_tac);
}
