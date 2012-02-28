/* vi: set sw=4 ts=4:
 *
 * tail.c - copy last lines from input to stdout.
 *
 * Copyright 2012 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/tail.html

USE_TAIL(NEWTOY(tail, "c-|fn-|", TOYFLAG_BIN))

config TAIL
	bool "tail"
	default n
	help
	  usage: tail [-n|c number] [-f] [file...]

	  Copy last lines from files to stdout. If no files listed, copy from
	  stdin. Filename "-" is a synonym for stdin.

	  -n	output the last X lines (default 10), +X counts from start.
	  -c    output the last X bytes, +X counts from start
	  -f   	follow file, waiting for more data to be appended
*/

#include "toys.h"

DEFINE_GLOBALS(
	long lines;
	long bytes;

	int file_no;
)

#define TT this.tail

#define FLAG_n 1
#define FLAG_f 2
#define FLAG_c 4
	
struct line_list {
	struct line_list *next;
	char *data;
	ssize_t len;
	long lines;
};

static void print_after_offset(int fd, long bytes, long lines)
{
	ssize_t read_len;
	long size=sizeof(toybuf);
	char c;
	
	while (bytes > 0 || lines > 0) {
		if (1>read(fd, &c, 1)) break;
		bytes--;
		if (c == '\n') lines--;
	}

	for (;;) {
		read_len = xread(fd, toybuf, size);
		if (read_len<1) break;
		xwrite(1, toybuf, read_len);
	}
}

static void print_last_bytes(int fd, long bytes)
{
	char *buf1, *buf2, *temp;
	ssize_t read_len;

	buf1 = xmalloc(bytes);
	buf2 = xmalloc(bytes);

	for(;;) {
		// swap buf1 and buf2
		temp = buf1;
		buf1 = buf2;
		buf2 = temp;

		read_len = readall(fd, buf2, bytes);
		if (read_len<bytes) break;
	}

	// output part of buf1 and all of buf2
	xwrite(1, buf1 + read_len, bytes - read_len);
	xwrite(1, buf2, read_len);

	if (CFG_TOYBOX_FREE) {
		free(buf1);
		free(buf2);
	}
}

static void free_line(void *data)
{
	struct line_list *list = (struct line_list *)data;
	free(list->data);
	free(list);
}

static void llist_add(struct line_list **head, struct line_list *new)
{
	struct line_list *cur = *head;

	new->next = NULL;
	if (cur) {
		while (cur->next)
			cur = cur->next;
		cur->next = new;
	} else *head = new;
}

static void print_last_lines(int fd, long lines)
{
	ssize_t i, total=0;
	long size=sizeof(toybuf);
	struct line_list *buf=NULL, *cur;

	for (;;) {
		// read from input and append to buffer list
		cur = xzalloc(sizeof(struct line_list));

		cur->data = xmalloc(size);
		cur->len = readall(fd, cur->data, size);
		llist_add(&buf, cur);

		// count newlines in latest input
		for (i=0; i<cur->len; i++)
			if (cur->data[i] == '\n') cur->lines++;
		total += cur->lines;

		// release first buffers if it leaves us enough newlines
		while (total - buf->lines > lines) {
			total -= buf->lines;
			free_line(llist_pop(&buf));
		}
		if (cur->len < size) break;
	}
	
	// if last buffer doesn't end in a newline, pretend like it did
	if (cur->data[cur->len - 1]!='\n') total++;
	
	// print out part of the first buffer
	i = 0;
	while (total>lines)
		if (buf->data[i++]=='\n') total--;
	xwrite(1, buf->data + i, buf->len - i);
	
	// print remaining buffers in their entirety
	for (cur=buf->next; cur; cur=cur->next)
		xwrite(1, cur->data, cur->len);

	if (CFG_TOYBOX_FREE) llist_free(buf, free_line);
}

static void do_tail(int fd, char *name)
{
	long lines=TT.lines, bytes=TT.bytes;

	if (toys.optc > 1) {
		// print an extra newline for all but the first file
		if (TT.file_no++) xputc('\n');
		xprintf("==> %s <==\n", name);
	}

	if (lines > 0 || bytes > 0) print_after_offset(fd, bytes, lines);
	else if (bytes < 0) print_last_bytes(fd, bytes * -1);
	else print_last_lines(fd, lines * -1);
}

void tail_main(void)
{
	// if nothing specified, default -n to -10
	if (!(toys.optflags&(FLAG_n|FLAG_c))) TT.lines = -10;

	loopfiles(toys.optargs, do_tail);
}
