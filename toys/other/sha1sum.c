/* vi: set sw=4 ts=4:
 *
 * sha1sum.c - Calculate sha1 cryptographic hash for input.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Based on the public domain SHA-1 in C by Steve Reid <steve@edmweb.com>
 * from http://www.mirrors.wiretapped.net/security/cryptography/hashes/sha1/

USE_SHA1SUM(NEWTOY(sha1sum, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config SHA1SUM
	bool "sha1sum"
	default y
	help
	  usage: sha1sum [file...]

	  Calculate sha1 hash of files (or stdin).
*/

#define FOR_sha1sum
#include <toys.h>

GLOBALS(
	uint32_t state[5];
	uint32_t oldstate[5];
	uint64_t count;
	union {
		unsigned char c[64];
		uint32_t i[16];
	} buffer;
)


static const unsigned rconsts[]={0x5A827999,0x6ED9EBA1,0x8F1BBCDC,0xCA62C1D6};

// Hash a single 512-bit block. This is the core of the algorithm.

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
static void sha1_transform(void)
{
	int i, j, k, count;
	unsigned *block = TT.buffer.i;
	unsigned *rot[5], *temp;

	// Copy context->state[] to working vars
	for (i=0; i<5; i++) {
		TT.oldstate[i] = TT.state[i];
		rot[i] = TT.state + i;
	}
	// 4 rounds of 20 operations each.
	for (i=count=0; i<4; i++) {
		for (j=0; j<20; j++) {
			unsigned work;

			work = *rot[2] ^ *rot[3];
			if (!i) work = (work & *rot[1]) ^ *rot[3];
			else {
				if (i==2)
					work = ((*rot[1]|*rot[2])&*rot[3])|(*rot[1]&*rot[2]);
				else work ^= *rot[1];
			}

			if (!i && j<16) work += block[count] = (rol(block[count],24)&0xFF00FF00) | (rol(block[count],8)&0x00FF00FF);
			else work += block[count&15] = rol(block[(count+13)&15]^block[(count+8)&15]^block[(count+2)&15]^block[count&15],1);
			*rot[4] += work + rol(*rot[0],5) + rconsts[i];
			*rot[1] = rol(*rot[1],30);

			// Rotate by one for next time.
			temp = rot[4];
			for (k=4; k; k--) rot[k] = rot[k-1];
			*rot = temp;
			count++;
		}
	}
	// Add the previous values of state[]
	for (i=0; i<5; i++) TT.state[i] += TT.oldstate[i];
}

// Fill the 64-byte working buffer and call sha1_transform() when full.

static void sha1_update(char *data, unsigned int len)
{
	unsigned int i, j;

	j = TT.count & 63;
	TT.count += len;

	// Enough data to process a frame?
	if ((j + len) > 63) {
		i = 64-j;
		memcpy(TT.buffer.c + j, data, i);
		sha1_transform();
		for ( ; i + 63 < len; i += 64) {
			memcpy(TT.buffer.c, data + i, 64);
			sha1_transform();
		}
		j = 0;
	} else i = 0;
	// Grab remaining chunk
	memcpy(TT.buffer.c + j, data + i, len - i);
}

// Callback for loopfiles()

static void do_sha1(int fd, char *name)
{
	uint64_t count;
	int i;
	char buf;

	/* SHA1 initialization constants */
	TT.state[0] = 0x67452301;
	TT.state[1] = 0xEFCDAB89;
	TT.state[2] = 0x98BADCFE;
	TT.state[3] = 0x10325476;
	TT.state[4] = 0xC3D2E1F0;
	TT.count = 0;

	for (;;) {
		i = read(fd, toybuf, sizeof(toybuf));
		if (i<1) break;
		sha1_update(toybuf, i);
	}

	count = TT.count << 3;

	// End the message by appending a "1" bit to the data, ending with the
	// message size (in bits, big endian), and adding enough zero bits in
	// between to pad to the end of the next 64-byte frame.
	//
	// Since our input up to now has been in whole bytes, we can deal with
	// bytes here too.

	buf = 0x80;
	do {
		sha1_update(&buf, 1);
		buf = 0;
	} while ((TT.count & 63) != 56);
	for (i = 0; i < 8; i++)
	  TT.buffer.c[56+i] = count >> (8*(7-i));
	sha1_transform();

	for (i = 0; i < 20; i++)
		toybuf[i] = TT.state[i>>2] >> ((3-(i & 3)) * 8);
	// Wipe variables.  Cryptogropher paranoia.
	memset(&TT, 0, sizeof(TT));

	for (i = 0; i < 20; i++) printf("%02x", toybuf[i]);
	printf("  %s\n", name);
}

void sha1sum_main(void)
{
	loopfiles(toys.optargs, do_sha1);
}
