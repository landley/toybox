/* vi: set sw=4 ts=4:
 *
 * sha1sum.c - Calculate sha1 cryptographic hash for input.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Based on the public domain SHA-1 in C by Steve Reid <steve@edmweb.com>
 * from http://www.mirrors.wiretapped.net/security/cryptography/hashes/sha1/
 *
 * Not in SUSv3.

USE_SHA1SUM(NEWTOY(sha1sum, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config SHA1SUM
	bool "sha1sum"
	default y
	help
	  usage: sha1sum [file...]

	  Calculate sha1 hash of files (or stdin).
*/

#include <toys.h>

struct sha1 {
	uint32_t state[5];
	uint32_t oldstate[5];
	uint64_t count;
	union {
		unsigned char c[64];
		uint32_t i[16];
	} buffer;
};

static void sha1_init(struct sha1 *this);
static void sha1_transform(struct sha1 *this);
static void sha1_update(struct sha1 *this, char *data, unsigned int len);
static void sha1_final(struct sha1 *this, char digest[20]);

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

// blk0() and blk() perform the initial expand.
// The idea of expanding during the round function comes from SSLeay
#if 1
#define blk0(i) (block[i] = (rol(block[i],24)&0xFF00FF00) \
	|(rol(block[i],8)&0x00FF00FF))
#else	// big endian?
#define blk0(i) block[i]
#endif
#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15] \
	^block[(i+2)&15]^block[i&15],1))

static const uint32_t rconsts[]={0x5A827999,0x6ED9EBA1,0x8F1BBCDC,0xCA62C1D6};

// Hash a single 512-bit block. This is the core of the algorithm.

static void sha1_transform(struct sha1 *this)
{
	int i, j, k, count;
	uint32_t *block = this->buffer.i;
	uint32_t *rot[5], *temp;

	// Copy context->state[] to working vars
	for (i=0; i<5; i++) {
		this->oldstate[i] = this->state[i];
		rot[i] = this->state + i;
	}
	// 4 rounds of 20 operations each.
	for (i=count=0; i<4; i++) {
		for (j=0; j<20; j++) {
			uint32_t work;

			work = *rot[2] ^ *rot[3];
			if (!i) work = (work & *rot[1]) ^ *rot[3];
			else {
				if (i==2)
					work = ((*rot[1]|*rot[2])&*rot[3])|(*rot[1]&*rot[2]);
				else work ^= *rot[1];
			}
			if (!i && j<16) work += blk0(count);
			else work += blk(count);
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
	for (i=0; i<5; i++) this->state[i] += this->oldstate[i];
}


// Initialize a struct sha1.

static void sha1_init(struct sha1 *this)
{
	/* SHA1 initialization constants */
	this->state[0] = 0x67452301;
	this->state[1] = 0xEFCDAB89;
	this->state[2] = 0x98BADCFE;
	this->state[3] = 0x10325476;
	this->state[4] = 0xC3D2E1F0;
	this->count = 0;
}

// Fill the 64-byte working buffer and call sha1_transform() when full.

void sha1_update(struct sha1 *this, char *data, unsigned int len)
{
	unsigned int i, j;

	j = this->count & 63;
	this->count += len;

	// Enough data to process a frame?
	if ((j + len) > 63) {
		i = 64-j;
		memcpy(this->buffer.c + j, data, i);
		sha1_transform(this);
		for ( ; i + 63 < len; i += 64) {
			memcpy(this->buffer.c, data + i, 64);
			sha1_transform(this);
		}
		j = 0;
	} else i = 0;
	// Grab remaining chunk
	memcpy(this->buffer.c + j, data + i, len - i);
}

// Add padding and return the message digest.

void sha1_final(struct sha1 *this, char digest[20])
{
	uint64_t count = this->count << 3;
	unsigned int i;
	char buf;

	// End the message by appending a "1" bit to the data, ending with the
	// message size (in bits, big endian), and adding enough zero bits in
	// between to pad to the end of the next 64-byte frame.
	//
	// Since our input up to now has been in whole bytes, we can deal with
	// bytes here too.

	buf = 0x80;
	do {
		sha1_update(this, &buf, 1);
		buf = 0;
	} while ((this->count & 63) != 56);
	for (i = 0; i < 8; i++)
	  this->buffer.c[56+i] = count >> (8*(7-i));
	sha1_transform(this);

	for (i = 0; i < 20; i++)
		digest[i] = this->state[i>>2] >> ((3-(i & 3)) * 8);
	// Wipe variables.  Cryptogropher paranoia.
	memset(this, 0, sizeof(struct sha1));
}

// Callback for loopfiles()

static void do_sha1(int fd, char *name)
{
	struct sha1 this;
	int len;

	sha1_init(&this);
	for (;;) {
		len = read(fd, toybuf, sizeof(toybuf));
		if (len<1) break;
		sha1_update(&this, toybuf, len);
	}
	sha1_final(&this, toybuf);
	for (len = 0; len < 20; len++) printf("%02x", toybuf[len]);
	printf("  %s\n", name);
}

void sha1sum_main(void)
{
	loopfiles(toys.optargs, do_sha1);
}
