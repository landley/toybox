/* vi: set sw=4 ts=4:
 *
 * od.c - Provide octal/hex dumps of data
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/od.html

USE_OD(NEWTOY(od, "j#vN#xsodcbA:t*", TOYFLAG_USR|TOYFLAG_BIN))

config OD
	bool "od"
	default y
	help
          usage: od [-bdosxv] [-j #] [-N #] [-A doxn] [-t arg]

	  -A	Address base (decimal, octal, hexdecimal, none)
	  -t	output type(s) a (ascii) c (char) d (decimal) foux
*/

#include "toys.h"

#define FLAG_t	(1 << 0)
#define FLAG_A	(1 << 1)
#define FLAG_b	(1 << 2)
#define FLAG_c	(1 << 3)
#define FLAG_d	(1 << 4)
#define FLAG_o	(1 << 5)
#define FLAG_s	(1 << 6)
#define FLAG_x	(1 << 7)
#define FLAG_N	(1 << 8)
#define FLAG_v  (1 << 9)

DEFINE_GLOBALS(
	struct arg_list *output_base;
	char *address_base;
	long max_count;
	long jump_bytes;

	unsigned types, leftover, star, address_idx;
	char *buf;
	uint64_t bufs[4]; // force 64-bit alignment
	off_t pos;
)

#define TT this.od

static char *ascii = "nulsohstxetxeotenqackbel bs ht nl vt ff cr so si"
	"dledc1dc2dc3dc4naksynetbcan emsubesc fs gs rs us sp";

struct odtype {
	int type;
	int size;
};

static void od_outline(void)
{
	unsigned flags = toys.optflags;
	char *abases[] = {"", "%07d", "%07o", "%06x"};
	struct odtype *types = (struct odtype *)toybuf, *t;
	int i, len;

	if (TT.leftover<16) memset(TT.buf+TT.leftover, 0, 16-TT.leftover);

	// Handle duplciate lines as *
	if (!(flags&FLAG_v) && TT.jump_bytes != TT.pos && TT.leftover
		&& !memcmp(TT.bufs, TT.bufs + 2, 16))
	{
		if (!TT.star) {
			xputs("*");
			TT.star++;
		}

	// Print line position
	} else {
		TT.star = 0;

		xprintf(abases[TT.address_idx], TT.pos);
		if (!TT.leftover) {
			if (TT.address_idx) xputc('\n');
			return;
		}
	}

	TT.pos += len = TT.leftover;
	TT.leftover = 0;
	if (TT.star) return;

	// For each output type, print one line

	for (i=0; i<TT.types; i++) {
		int j = 0, pad = i ? 8 : 0;
		char buf[128];

		t = types+i;
		while (j<len) {
			unsigned k;

			// Handle ascii
			if (t->type < 2) {
				char c = TT.buf[j++];
				pad += 4;

				if (!t->type) {
					c &= 127;
					if (c<=32) sprintf(buf, "%.3s", ascii+(3*c));
					else if (c==127) strcpy(buf, "del");
					else sprintf(buf, "%c", c);
				} else {
					char *bfnrt = "\b\f\n\r\t", *s = strchr(bfnrt, c);
					if (s) sprintf(buf, "\\%c", "bfnrt0"[s-bfnrt]);
					else {
						// TODO: this should be UTF8 aware.
						sprintf(buf, "%c", c);
					}
				}
			} else if (CFG_TOYBOX_FLOAT && t->type == 6) {
				// TODO: floating point stuff
			// Integer types
			} else {
				unsigned long long ll = 0, or;
				int throw = 0;
				char *c[] = {"%*lld", "%*llu", "%0*llo", "%0*llx"},
					*class = c[t->type-2];

				// Work out width of field
				if (t->size == 8) {
					or = -1LL;
					if (t->type == 2) or >>= 1;
				} else or = (1LL<<(8*t->size))-1;
				throw = sprintf(buf, class, 0, or);

				// Accumulate integer based on size argument
				for (k=0; k < t->size; k++) {
					or = TT.buf[j++];
					ll |= or << (8*(IS_BIG_ENDIAN ? t->size-k-1 : k));
				}

				// Handle negative values
				if (t->type == 2) {
					or = sizeof(or) - t->size;
					throw++;
					if (or && (ll & (1l<<((8*t->size)-1))))
						ll |= ((or<<(8*or))-1) << (8*t->size);
				}

				sprintf(buf, class, throw, ll);
				pad += throw+1;
			}
			xprintf("%*s", pad, buf);
			pad = 0;
		}
		xputc('\n');
	}

	// buffer toggle for "same as last time" check.
	TT.buf = (char *)((TT.buf == (char *)TT.bufs) ? TT.bufs+2 : TT.bufs);
}

static void do_od(int fd, char *name)
{
	// Skip input, possibly more than one entire file.
	if (TT.jump_bytes < TT.pos) {
		off_t off = lskip(fd, TT.jump_bytes);
		if (off > 0) TT.pos += off;
		if (TT.jump_bytes < TT.pos) return;
	}

	for(;;) {
		char *buf = TT.buf + TT.leftover;
		int len = 16 - TT.leftover;

		if (toys.optflags & FLAG_N) {
			if (!TT.max_count) break;
			if (TT.max_count < len) len = TT.max_count;
		}

		len = readall(fd, buf, len);
		if (len < 0) {
			perror_msg("%s", name);
			break;
		}
		if (TT.max_count) TT.max_count -= len;
		TT.leftover += len;
		if (TT.leftover < 16) break;

		od_outline();
	}
}

static void append_base(char *base)
{
	char *s = base;
	struct odtype *types = (struct odtype *)toybuf;
	int type;

	for (;;) {
		int size = 1;

		if (!*s) return;
		if (TT.types >= sizeof(toybuf)/sizeof(struct odtype)) break;
		if (-1 == (type = stridx("acduox"USE_TOYBOX_FLOAT("f"), *(s++)))) break;

		if (isdigit(*s)) {
			size = strtol(s, &s, 10);
			if (type < 2 && size != 1) break;
			if (CFG_TOYBOX_FLOAT && type == 6 && size == sizeof(long double));
			else if (size < 0 || size > 8) break;
		} else if (CFG_TOYBOX_FLOAT && type == 6) {
			int sizes[] = {sizeof(float), sizeof(double), sizeof(long double)};
			if (-1 == (size = stridx("FDL", *s))) size = sizeof(double);
			else {
				s++;
				size = sizes[size];
			}
		} else if (type > 1) {
			if (-1 == (size = stridx("CSIL", *s))) size = 4;
			else {
				s++;
				size = 1 << size;
			}
		}

		types[TT.types].type = type;
		types[TT.types].size = size;
		TT.types++;
	}

	error_exit("bad -t %s", base);
}

void od_main(void)
{
	struct arg_list *arg;

	TT.buf = (char *)TT.bufs;

	if (!TT.address_base) TT.address_idx = 2;
	else if (0>(TT.address_idx = stridx("ndox", *TT.address_base)))
		error_exit("bad -A '%c'", *TT.address_base);

	// Collect -t entries

	for (arg = TT.output_base; arg; arg = arg->next) append_base(arg->arg);
	if (toys.optflags & FLAG_b) append_base("o1");
	if (toys.optflags & FLAG_d) append_base("u2");
	if (toys.optflags & FLAG_o) append_base("o2");
	if (toys.optflags & FLAG_s) append_base("d2");
	if (toys.optflags & FLAG_x) append_base("x2");
	if (!TT.output_base) append_base("o2");

	loopfiles(toys.optargs, do_od);

	if (TT.leftover) od_outline();
	od_outline();
}
