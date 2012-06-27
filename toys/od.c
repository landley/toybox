/* vi: set sw=4 ts=4:
 *
 * od.c - Provide octal/hex dumps of data
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/od.html

USE_OD(NEWTOY(od, "j#vN#xsodbA:t*", TOYFLAG_USR|TOYFLAG_BIN))

config OD
	bool "od"
	default y
	help
          usage: od [-bdosxv] [-j #] [-N #] [-A arg] [-t arg]
*/

#include "toys.h"

#define OD_BLOCK_SIZE 16

#define FLAG_t	(1 << 0)
#define FLAG_A	(1 << 1)
#define FLAG_b	(1 << 2)
#define FLAG_d	(1 << 3)
#define FLAG_o	(1 << 4)
#define FLAG_s	(1 << 5)
#define FLAG_x	(1 << 6)
#define FLAG_N	(1 << 7)
#define FLAG_v  (1 << 8)


DEFINE_GLOBALS(
		struct arg_list *output_base;
		char *address_base;
		long max_count;
		long jump_bytes;
)

#define TT this.od

static const char *ascii = "nulsohstxetceotenqackbel bs ht nl vt ff cr so si"
	"deldc1dc2dc3dc4naksynetbcan emsubesc fs gs rs us sp";

static void display_line_1(char base, off_t offset, uint8_t *line, int len)
{
	while (len--) {
		switch (base) {
			case 'a': {
				int ch = *line & 0x7f;
				if (ch <= 32) printf(" %.3s", ascii+(3*ch));
				else if (ch == 127) printf(" del");
				else printf("%4c", ch);
				break;
			}
			case 'o': printf(" %3.3o", *line); break;
			case 'x': printf(" %2.2x", *line); break;
			case 'd': printf(" %4d", *(int8_t *)line); break;
			case 'u': printf(" %3u", *line); break;
		}
		line++;
	}
}

static void display_line_2(char base, off_t offset, uint16_t *line, int len)
{
	while (len--) {
		switch (base) {
			case 'o': printf(" %6.6o", *line); break;
			case 'x': printf(" %4.4x", *line); break;
			case 'd': printf(" %6d", *(int16_t*)line); break;
			case 'u': printf(" %5u", *line); break;
		}
		line++;
	}
}

static void display_line_4(char base, off_t offset, uint32_t *line, int len)
{
	while (len--) {
		switch (base) {
			case 'o': printf(" %11.11o", *line); break;
			case 'x': printf(" %8.8x", *line); break;
			case 'd': printf(" %11d", *(int32_t *)line); break;
			case 'u': printf(" %10u", *line); break;
			case 'f': printf(" %15g", *(float *)line); break;
		}
		line++;
	}
}

static void display_line_8(char base, off_t offset, uint64_t *line, int len)
{
	while (len--) {
		switch (base) {
			//case 'o': printf(" %22.22o", *line); break;
			case 'x': printf(" %16.16" PRIx64, *line); break;
			case 'd': printf(" %21" PRId64, *(int64_t *)line); break;
			case 'u': printf(" %20" PRIu64, *line); break;
			case 'f': printf(" %23g", *(double *)line);
		}
		line++;
	}
}

static void display_line(off_t offset, uint8_t *line, int len)
{
	struct arg_list *output = TT.output_base;
	switch (*TT.address_base) {
		case 'o': printf("%8.8zo", offset); break;
		case 'd': printf("%7.7zd", offset); break;
		case 'x': printf("%6.6zx", offset); break;
		case 'n':
		default:
			  break;
	}
	while (output) {
		char base = *output->arg;
		int width = atoi(&output->arg[1]);

		switch (width) {
			case 1: display_line_1(base, offset, line, len); break;
			case 2: display_line_2(base, offset, (uint16_t *)line,
						(len + 1) / 2); break;
			case 4: display_line_4(base, offset, (uint32_t *)line,
						(len + 3) / 4); break;
			case 8: display_line_8(base, offset, (uint64_t *)line,
						(len + 7) / 8); break;
		}
		output = output->next;
	}

	printf("\n");
}

static void do_od(int fd, char *name)
{
	uint8_t *block[2];
	int index = 0;
	off_t offset = 0;
	int last_match = 0;

	block[0] = (uint8_t *)toybuf;
	block[1] = (uint8_t *)&toybuf[OD_BLOCK_SIZE];

	if (TT.jump_bytes) {
		xlseek(fd, TT.jump_bytes, SEEK_SET);
		offset = TT.jump_bytes;
	}

	for (;;) {
		int len;
		int max_len = OD_BLOCK_SIZE;
		if ((toys.optflags & FLAG_N) && offset + OD_BLOCK_SIZE > TT.max_count)
			max_len = TT.max_count - offset;
		len = xread(fd, block[index], max_len);
		if (!len) break;

		memset(&block[index][len], 0, OD_BLOCK_SIZE - len);

		if (!(toys.optflags & FLAG_v) && offset > 0 &&
				memcmp(block[0], block[1], OD_BLOCK_SIZE) == 0)
		{
			if (!last_match) puts("*");
			last_match = 1;
		} else {
			display_line(offset, block[index], len);
			last_match = 0;
		}
		offset += len;
		index = !index;

		if (len != OD_BLOCK_SIZE) break;
	}

	if (!(toys.optflags & FLAG_N) && offset != TT.max_count)
		display_line(offset, NULL, 0);
}

static void append_base(char *base)
{
	struct arg_list *arg, *prev;
	arg = xmalloc(sizeof(struct arg_list));
	prev = TT.output_base;
	TT.output_base = arg;
	arg->arg = base;
	arg->next = prev;
}

static void valid_bases(void)
{
	struct arg_list *arg = TT.output_base;
	while (arg) {
		char base = arg->arg[0];
		int width = 1;
		if (arg->arg[1])
			width = atoi(&arg->arg[1]);
		switch (base) {
		case 'a':
			if (width != 1)
				error_exit("invalid width for ascii base");
			break;
		case 'd': case 'x': case 'o': case'u':
			if (width != 1 && width != 2 && width != 4 && width != 8)
				error_exit("this system doesn't provide a %d-byte type", width);
			break;
		case 'f':
			if (width != 4 && width != 8)
				error_exit("this system doesn't provide a %d-byte floating point type",
						width);
			break;
		default:
			error_exit("invalid base '%c'", base);
		}

		arg = arg->next;
	};
}

void od_main(void)
{
	if (!TT.address_base) TT.address_base = "o";
	if (toys.optflags & FLAG_b) append_base("o1");
	if (toys.optflags & FLAG_d) append_base("u2");
	if (toys.optflags & FLAG_o) append_base("o2");
	if (toys.optflags & FLAG_s) append_base("d2");
	if (toys.optflags & FLAG_x) append_base("x2");
	if (!TT.output_base) append_base("o2");
	valid_bases();
	loopfiles(toys.optargs, do_od);
}
