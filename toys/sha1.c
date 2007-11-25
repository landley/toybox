/*
 * Based on the public domain SHA-1 in C by Steve Reid <steve@edmweb.com>
 * from http://www.mirrors.wiretapped.net/security/cryptography/hashes/sha1/
 */

/* #define LITTLE_ENDIAN * This should be #define'd if true. */
/* #define SHA1HANDSOFF * Copies data before messing with it. */
#define	LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

struct sha1 {
	uint32_t state[5];
	uint64_t count;
	union {
		unsigned char c[64];
		uint32_t i[16];
	} buffer;
};

void sha1_init(struct sha1 *this);
void sha1_transform(struct sha1 *this);
void sha1_update(struct sha1 *this, unsigned char *data, unsigned int len);
void sha1_final(struct sha1 *this, unsigned char digest[20]);

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#ifdef LITTLE_ENDIAN
#define blk0(i) (block[i] = (rol(block[i],24)&0xFF00FF00) \
	|(rol(block[i],8)&0x00FF00FF))
#else
#define blk0(i) block[i]
#endif
#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15] \
	^block[(i+2)&15]^block[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

void printy(unsigned char *this)
{
	int i;

	for (i = 0; i < 20; i++) printf("%02x", this[i]);
	putchar('\n');
}

/* Hash a single 512-bit block. This is the core of the algorithm. */

void sha1_transform(struct sha1 *this)
{
	unsigned int a, b, c, d, e;
	uint32_t *block = this->buffer.i;

	/* Copy context->state[] to working vars */
	a = this->state[0];
	b = this->state[1];
	c = this->state[2];
	d = this->state[3];
	e = this->state[4];
	/* 4 rounds of 20 operations each. Loop unrolled. */
	R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
	R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
	R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
	R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
	R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
	R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
	R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
	R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
	R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
	R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
	R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
	R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
	R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
	R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
	R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
	R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
	R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
	R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
	R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
	R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
	/* Add the working vars back into context.state[] */
	this->state[0] += a;
	this->state[1] += b;
	this->state[2] += c;
	this->state[3] += d;
	this->state[4] += e;
printy(this->state);
	/* Wipe variables */
	a = b = c = d = e = 0;
}


/* SHA1Init - Initialize new context */

void sha1_init(struct sha1 *this)
{
	/* SHA1 initialization constants */
	this->state[0] = 0x67452301;
	this->state[1] = 0xEFCDAB89;
	this->state[2] = 0x98BADCFE;
	this->state[3] = 0x10325476;
	this->state[4] = 0xC3D2E1F0;
	this->count = 0;
}

/* Run your data through this function. */

void sha1_update(struct sha1 *this, unsigned char *data, unsigned int len)
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

/* Add padding and return the message digest. */

void sha1_final(struct sha1 *this, unsigned char digest[20])
{
	uint64_t count = this->count << 3;
	unsigned int i;
	unsigned char buf;

	// End the message by appending a "1" bit to the data, ending with the
	// message size (in bits, big endian), and adding enough zero bits in
	// between to pad to the end of the next 64-byte frame.  Since our input
	// up to now has been in whole bytes, we can deal with bytes here too.

	buf = 0x80;
	do {
		sha1_update(this, &buf, 1);
		buf = 0;
	} while ((this->count & 63) != 56);
	for (i = 0; i < 8; i++)
	  this->buffer.c[56+i] = count >> (8*(7-i));
	sha1_transform(this);

	for (i = 0; i < 20; i++) {
		digest[i] = (unsigned char)
		 ((this->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
	}
	/* Wipe variables */
	i = 0;
}


/*************************************************************/


int main(int argc, char** argv)
{
	int i, j;
	struct sha1 this;
	unsigned char digest[20], buffer[16384];
	FILE* file;

	if (argc < 2) {
		file = stdin;
	}
	else {
		if (!(file = fopen(argv[1], "rb"))) {
			fputs("Unable to open file.", stderr);
			exit(-1);
		}
	}
	sha1_init(&this);
	while (!feof(file)) {  /* note: what if ferror(file) */
		i = fread(buffer, 1, 16384, file);
		sha1_update(&this, buffer, i);
	}
	sha1_final(&this, digest);
	fclose(file);
	printy(digest);
	exit(0);
}
