/* vi: set sw=4 ts=4: */
/* micro-bunzip, a small, simple bzip2 decompression implementation.

   Copyright 2003, 2006 by Rob Landley (rob@landley.net).

   Based on a close reading (but not the actual code) of the original bzip2
   decompression code by Julian R Seward (jseward@acm.org), which also
   acknowledges contributions by Mike Burrows, David Wheeler, Peter Fenwick,
   Alistair Moffat, Radford Neal, Ian H. Witten, Robert Sedgewick, and
   Jon L. Bentley.
*/

#include "toys.h"

// Constants for huffman coding
#define MAX_GROUPS			6
#define GROUP_SIZE   		50		/* 64 would have been more efficient */
#define MAX_HUFCODE_BITS 	20		/* Longest huffman code allowed */
#define MAX_SYMBOLS 		258		/* 256 literals + RUNA + RUNB */
#define SYMBOL_RUNA			0
#define SYMBOL_RUNB			1

// Other housekeeping constants
#define IOBUF_SIZE			4096

// Status return values
#define RETVAL_OK						0
#define RETVAL_LAST_BLOCK				(-1)
#define RETVAL_NOT_BZIP_DATA			(-2)
#define RETVAL_DATA_ERROR				(-3)
#define RETVAL_OBSOLETE_INPUT			(-4)

char *bunzip_errors[]={
	NULL,
	"Not bzip data",
	"Data error",
	"Out of memory",
	"Obsolete (pre 0.9.5) bzip format not supported."
};

// This is what we know about each huffman coding group
struct group_data {
	int limit[MAX_HUFCODE_BITS+1], base[MAX_HUFCODE_BITS], permute[MAX_SYMBOLS];
	char minLen, maxLen;
};

// Structure holding all the housekeeping data, including IO buffers and
// memory that persists between calls to bunzip
typedef struct {

	// Input stream, input buffer, input bit buffer
	int in_fd, inbufCount, inbufPos;
	char *inbuf;
	unsigned int inbufBitCount, inbufBits;

	// Output buffer
	char outbuf[IOBUF_SIZE];
	int outbufPos;

	// The CRC values stored in the block header and calculated from the data
	unsigned int crc32Table[256], headerCRC, dataCRC, totalCRC;

	// Intermediate buffer and its size (in bytes)
	unsigned int *dbuf, dbufSize;

	// State for interrupting output loop
	int writePos, writeRun, writeCount, writeCurrent;

	// These things are a bit too big to go on the stack
	char selectors[32768];                  // nSelectors=15 bits
	struct group_data groups[MAX_GROUPS];   // huffman coding tables
} bunzip_data;

// Return the next nnn bits of input.  All reads from the compressed input
// are done through this function.  All reads are big endian.
static unsigned int get_bits(bunzip_data *bd, char bits_wanted)
{
	unsigned int bits = 0;

	// If we need to get more data from the byte buffer, do so.  (Loop getting
	// one byte at a time to enforce endianness and avoid unaligned access.)
	while (bd->inbufBitCount < bits_wanted) {

		// If we need to read more data from file into byte buffer, do so
		if (bd->inbufPos == bd->inbufCount) {
			if (0 >= (bd->inbufCount = read(bd->in_fd, bd->inbuf, IOBUF_SIZE)))
				error_exit("Unexpected input EOF");
			bd->inbufPos = 0;
		}

		// Avoid 32-bit overflow (dump bit buffer to top of output)
		if (bd->inbufBitCount>=24) {
			bits = bd->inbufBits&((1<<bd->inbufBitCount)-1);
			bits_wanted -= bd->inbufBitCount;
			bits <<= bits_wanted;
			bd->inbufBitCount = 0;
		}

		// Grab next 8 bits of input from buffer.
		bd->inbufBits = (bd->inbufBits<<8) | bd->inbuf[bd->inbufPos++];
		bd->inbufBitCount += 8;
	}

	// Calculate result
	bd->inbufBitCount -= bits_wanted;
	bits |= (bd->inbufBits>>bd->inbufBitCount) & ((1<<bits_wanted)-1);

	return bits;
}

// Decompress a block of text to intermediate buffer
int read_bunzip_data(bunzip_data *bd)
{
	struct group_data *hufGroup GCC_BUG;
	unsigned origPtr;
	int dbufCount, nextSym, dbufSize, groupCount, *base GCC_BUG, *limit GCC_BUG,
		selector, i, j, k, t, runPos, symCount, symTotal, nSelectors,
		byteCount[256];
	char uc, mtfSymbol[256], symToByte[256], *selectors;
	unsigned int *dbuf;

	// Read in header signature and CRC (which is stored big endian)
	i = get_bits(bd, 24);
	j = get_bits(bd, 24);
	bd->headerCRC = get_bits(bd,32);

	// Is this the EOF block with CRC for whole file?
	if (i==0x177245 && j==0x385090) return RETVAL_LAST_BLOCK;

	// Is this a valid data block?
	if (i!=0x314159 || j!=0x265359) return RETVAL_NOT_BZIP_DATA;

	dbuf = bd->dbuf;
	dbufSize = bd->dbufSize;
	selectors = bd->selectors;

	// We can add support for blockRandomised if anybody complains.
	if (get_bits(bd,1)) return RETVAL_OBSOLETE_INPUT;
	if ((origPtr = get_bits(bd,24)) > dbufSize) return RETVAL_DATA_ERROR;

	// mapping table: if some byte values are never used (encoding things
	// like ascii text), the compression code removes the gaps to have fewer
	// symbols to deal with, and writes a sparse bitfield indicating which
	// values were present.  We make a translation table to convert the symbols
	// back to the corresponding bytes.
	t = get_bits(bd, 16);
	symTotal = 0;
	for (i=0; i<16; i++) {
		if (t&(1<<(15-i))) {
			k = get_bits(bd,16);
			for (j=0;j<16;j++)
				if (k&(1<<(15-j))) symToByte[symTotal++] = (16*i)+j;
		}
	}

	// How many different huffman coding groups does this block use?
	groupCount = get_bits(bd,3);
	if (groupCount<2 || groupCount>MAX_GROUPS) return RETVAL_DATA_ERROR;

	// nSelectors: Every GROUP_SIZE many symbols we select a new huffman coding
	// group.  Read in the group selector list, which is stored as MTF encoded
	// bit runs.  (MTF = Move To Front.  Every time a symbol occurs it's moved
	// to the front of the table, so it has a shorter encoding next time.)
	if (!(nSelectors = get_bits(bd, 15))) return RETVAL_DATA_ERROR;
	for (i=0; i<groupCount; i++) mtfSymbol[i] = i;
	for (i=0; i<nSelectors; i++) {

		// Get next value
		for(j=0;get_bits(bd,1);j++)
			if (j>=groupCount) return RETVAL_DATA_ERROR;

		// Decode MTF to get the next selector, and move it to the front.
		uc = mtfSymbol[j];
		memmove(mtfSymbol+1, mtfSymbol, j);
		mtfSymbol[0] = selectors[i] = uc;
	}
	// Read the huffman coding tables for each group, which code for symTotal
	// literal symbols, plus two run symbols (RUNA, RUNB)
	symCount = symTotal+2;
	for (j=0; j<groupCount; j++) {
		unsigned char length[MAX_SYMBOLS], temp[MAX_HUFCODE_BITS+1];
		int	minLen,	maxLen, pp;

		// Read lengths
		t = get_bits(bd, 5);
		for (i = 0; i < symCount; i++) {
			for(;;) {
				// !t || t > MAX_HUFCODE_BITS in one test.
				if (MAX_HUFCODE_BITS-1 < (unsigned)t-1)
					return RETVAL_DATA_ERROR;
				// Grab 2 bits instead of 1 (slightly smaller/faster).  Stop if
				// first bit is 0, otherwise second bit says whether to
				// increment or decrement.
				k = get_bits(bd, 2);
				if (k & 2) t += 1 - ((k&1)<<1);
				else {
					bd->inbufBitCount++;
					break;
				}
			}
			length[i] = t;
		}

		// Find largest and smallest lengths in this group
		minLen = maxLen = length[0];
		for (i = 1; i < symCount; i++) {
			if(length[i] > maxLen) maxLen = length[i];
			else if(length[i] < minLen) minLen = length[i];
		}

		/* Calculate permute[], base[], and limit[] tables from length[].
		 *
		 * permute[] is the lookup table for converting huffman coded symbols
		 * into decoded symbols.  It contains symbol values sorted by length.
		 *
		 * base[] is the amount to subtract from the value of a huffman symbol
		 * of a given length when using permute[].
		 *
		 * limit[] indicates the largest numerical value a symbol with a given
		 * number of bits can have.  It lets us know when to stop reading.
		 *
		 * To use these, keep reading bits until value <= limit[bitcount] or
		 * you've read over 20 bits (error).  Then the decoded symbol
		 * equals permute[hufcode_value - base[hufcode_bitcount]].
		 */
		hufGroup = bd->groups+j;
		hufGroup->minLen = minLen;
		hufGroup->maxLen = maxLen;

		// Note that minLen can't be smaller than 1, so we adjust the base
		// and limit array pointers so we're not always wasting the first
		// entry.  We do this again when using them (during symbol decoding).
		base = hufGroup->base-1;
		limit = hufGroup->limit-1;

		// Calculate permute[], and zero temp[] and limit[].
		pp = 0;
		for (i = minLen; i <= maxLen; i++)
			temp[i] = limit[i] = 0;
			for (t = 0; t < symCount; t++)
				if (length[t] == i) hufGroup->permute[pp++] = t;

		// Count symbols coded for at each bit length
		for (i = 0; i < symCount; i++) temp[length[i]]++;

		/* Calculate limit[] (the largest symbol-coding value at each bit
		 * length, which is (previous limit<<1)+symbols at this level), and
		 * base[] (number of symbols to ignore at each bit length, which is
		 * limit minus the cumulative count of symbols coded for already). */
		pp = t = 0;
		for (i = minLen; i < maxLen; i++) {
			pp += temp[i];
			limit[i] = pp-1;
			pp <<= 1;
			base[i+1] = pp-(t+=temp[i]);
		}
		limit[maxLen] = pp+temp[maxLen]-1;
		limit[maxLen+1] = INT_MAX;
		base[minLen] = 0;
	}

	// We've finished reading and digesting the block header.  Now read this
	// block's huffman coded symbols from the file and undo the huffman coding
	// and run length encoding, saving the result into dbuf[dbufCount++] = uc

	// Initialize symbol occurrence counters and symbol mtf table
	for(i=0; i<256; i++) {
		byteCount[i] = 0;
		mtfSymbol[i] = i;
	}

	// Loop through compressed symbols.  This is the first "tight inner loop"
	// that needs to be micro-optimized for speed.  (This one fills out dbuf[]
	// linearly, staying in cache more, so isn't as limited by DRAM access.)
	runPos = dbufCount = symCount = selector = 0;
	for (;;) {

		// Determine which huffman coding group to use.
		if (!(symCount--)) {
			symCount = GROUP_SIZE-1;
			if (selector >= nSelectors) return RETVAL_DATA_ERROR;
			hufGroup = bd->groups+selectors[selector++];
			base = hufGroup->base-1;
			limit = hufGroup->limit-1;
		}

		// Read next huffman-coded symbol
		i = hufGroup->minLen;
		j = get_bits(bd, i);
		while (j > limit[i]) {
			// if (i > hufGroup->maxLen) return RETVAL_DATA_ERROR;
			i++;

			// Unroll get_bits() to avoid a function call when the data's in
			// the buffer already.
			k = bd->inbufBitCount
			   	? (bd->inbufBits >> --(bd->inbufBitCount)) & 1
				: get_bits(bd, 1);
			j = (j << 1) | k;
		}

		// Huffman decode nextSym (with bounds checking)
		j-=base[i];
		if (i > hufGroup->maxLen || (unsigned)j >= MAX_SYMBOLS)
			return RETVAL_DATA_ERROR;
		nextSym = hufGroup->permute[j];

		// If this is a repeated run, loop collecting data
		if ((unsigned)nextSym <= SYMBOL_RUNB) {

			// If this is the start of a new run, zero out counter
			if(!runPos) {
				runPos = 1;
				t = 0;
			}

			/* Neat trick that saves 1 symbol: instead of or-ing 0 or 1 at
			   each bit position, add 1 or 2 instead.  For example,
			   1011 is 1<<0 + 1<<1 + 2<<2.  1010 is 2<<0 + 2<<1 + 1<<2.
			   You can make any bit pattern that way using 1 less symbol than
			   the basic or 0/1 method (except all bits 0, which would use no
			   symbols, but a run of length 0 doesn't mean anything in this
			   context).  Thus space is saved. */
			t += (runPos << nextSym); // +runPos if RUNA; +2*runPos if RUNB
			runPos <<= 1;
			continue;
		}

		/* When we hit the first non-run symbol after a run, we now know
		   how many times to repeat the last literal, so append that many
		   copies to our buffer of decoded symbols (dbuf) now.  (The last
		   literal used is the one at the head of the mtfSymbol array.) */
		if (runPos) {
			runPos = 0;
			if (dbufCount+t >= dbufSize) return RETVAL_DATA_ERROR;

			uc = symToByte[mtfSymbol[0]];
			byteCount[uc] += t;
			while (t--) dbuf[dbufCount++] = uc;
		}

		// Is this the terminating symbol?
		if (nextSym>symTotal) break;

		/* At this point, the symbol we just decoded indicates a new literal
		   character.  Subtract one to get the position in the MTF array
		   at which this literal is currently to be found.  (Note that the
		   result can't be -1 or 0, because 0 and 1 are RUNA and RUNB.
		   Another instance of the first symbol in the mtf array, position 0,
		   would have been handled as part of a run.) */
		if (dbufCount>=dbufSize) return RETVAL_DATA_ERROR;
		i = nextSym - 1;
		uc = mtfSymbol[i];
		// On my laptop, unrolling this memmove() into a loop shaves 3.5% off
		// the total running time.
		while(i--) mtfSymbol[i+1] = mtfSymbol[i];
		mtfSymbol[0] = uc;
		uc = symToByte[uc];

		// We have our literal byte.  Save it into dbuf.
		byteCount[uc]++;
		dbuf[dbufCount++] = (unsigned int)uc;
	}

	/* At this point, we've finished reading all of this block's huffman-coded
	 * symbols (and repeated runs) from the input stream, and have written
	 * dbufCount many of them into dbuf[], the intermediate buffer.
	 *
	 * Now undo the Burrows-Wheeler transform on dbuf, described here:
	 * http://dogma.net/markn/articles/bwt/bwt.htm
	 * http://marknelson.us/1996/09/01/bwt/
	 */

	// Now we know what dbufCount is, do a better sanity check on origPtr.
	if (origPtr>=dbufCount) return RETVAL_DATA_ERROR;

	// Turn byteCount into cumulative occurrence counts of 0 to n-1.
	j = 0;
	for (i=0;i<256;i++) {
		k = j+byteCount[i];
		byteCount[i] = j;
		j = k;
	}

	// Figure out what order dbuf would be in if we sorted it.
	for (i=0; i<dbufCount; i++) {
		uc = (unsigned char)(dbuf[i] & 0xff);
		dbuf[byteCount[uc]] |= (i << 8);
		byteCount[uc]++;
	}

	// blockRandomised support would go here.

	// Using i as position, j as previous character, t as current character,
	// and uc as run count.
	bd->dataCRC = 0xffffffffL;

	/* Decode first byte by hand to initialize "previous" byte.  Note that it
	   doesn't get output, and if the first three characters are identical
	   it doesn't qualify as a run (hence uc=255, which will either wrap
	   to 1 or get reset). */
	if (dbufCount) {
		bd->writePos = dbuf[origPtr];
	    bd->writeCurrent = (unsigned char)(bd->writePos&0xff);
		bd->writePos >>= 8;
		bd->writeRun = -1;
	}
	bd->writeCount = dbufCount;

	return RETVAL_OK;
}

// Flush output buffer to disk
void flush_bunzip_outbuf(bunzip_data *bd, int out_fd)
{
	if (bd->outbufPos) {
		if (write(out_fd, bd->outbuf, bd->outbufPos) != bd->outbufPos)
			error_exit("Unexpected output EOF");
		bd->outbufPos = 0;
	}
}

// Undo burrows-wheeler transform on intermediate buffer to produce output.
// If !len, write up to len bytes of data to buf.  Otherwise write to out_fd.
// Returns len ? bytes written : RETVAL_OK.  Notice all errors negative #'s.
int write_bunzip_data(bunzip_data *bd, int out_fd, char *outbuf, int len)
{
	unsigned int *dbuf = bd->dbuf;
	int count, pos, current, run, copies, outbyte, previous, gotcount = 0;

	for (;;) {

		// If last read was short due to end of file, return last block now
		if (bd->writeCount < 0) return bd->writeCount;

		// If we need to refill dbuf, do it.
		if (!bd->writeCount) {
			int i = read_bunzip_data(bd);
			if (i) {
				if (i == RETVAL_LAST_BLOCK) {
					bd->writeCount = i;
					return gotcount;
				} else return i;
			}
		}

		// Loop generating output
		count = bd->writeCount;
		pos = bd->writePos;
		current = bd->writeCurrent;
		run = bd->writeRun;
		while (count) {

			// If somebody (like tar) wants a certain number of bytes of
			// data from memory instead of written to a file, humor them.
			if (len && bd->outbufPos>=len) goto dataus_interruptus;
			count--;

			// Follow sequence vector to undo Burrows-Wheeler transform.
			previous = current;
			pos = dbuf[pos];
			current = pos&0xff;
			pos >>= 8;

			// Whenever we see 3 consecutive copies of the same byte,
			// the 4th is a repeat count
			if (run++ == 3) {
				copies = current;
				outbyte = previous;
				current = -1;
			} else {
				copies = 1;
				outbyte = current;
			}

			// Output bytes to buffer, flushing to file if necessary
			while (copies--) {
				if (bd->outbufPos == IOBUF_SIZE) flush_bunzip_outbuf(bd,out_fd);
				bd->outbuf[bd->outbufPos++] = outbyte;
				bd->dataCRC = (bd->dataCRC << 8)
								^ bd->crc32Table[(bd->dataCRC >> 24) ^ outbyte];
			}
			if (current!=previous) run=0;
		}

		// Decompression of this block completed successfully
		bd->dataCRC = ~(bd->dataCRC);
		bd->totalCRC = ((bd->totalCRC << 1) | (bd->totalCRC >> 31))
			^ bd->dataCRC;

		// If this block had a CRC error, force file level CRC error.
		if (bd->dataCRC != bd->headerCRC) {
			bd->totalCRC = bd->headerCRC+1;

			return RETVAL_LAST_BLOCK;
		}
dataus_interruptus:
		bd->writeCount = count;
		if (len) {
			gotcount += bd->outbufPos;
			memcpy(outbuf, bd->outbuf, len);

			// If we got enough data, checkpoint loop state and return
			if ((len-=bd->outbufPos)<1) {
				bd->outbufPos -= len;
				if (bd->outbufPos)
					memmove(bd->outbuf, bd->outbuf+len, bd->outbufPos);
				bd->writePos = pos;
				bd->writeCurrent = current;
				bd->writeRun = run;

				return gotcount;
			}
		}
	}
}

// Allocate the structure, read file header.  If !len, src_fd contains
// filehandle to read from.  Else inbuf contains data.
int start_bunzip(bunzip_data **bdp, int src_fd, char *inbuf, int len)
{
	bunzip_data *bd;
	unsigned int i, j, c;

	// Figure out how much data to allocate.
	i = sizeof(bunzip_data);
	if (!len) i += IOBUF_SIZE;

	// Allocate bunzip_data.  Most fields initialize to zero.
	bd = *bdp = xzalloc(i);
	if (len) {
		bd->inbuf = inbuf;
		bd->inbufCount = len;
		bd->in_fd = -1;
	} else {
		bd->inbuf = (char *)(bd+1);
		bd->in_fd = src_fd;
	}

	// Init the CRC32 table (big endian)
	for (i=0; i<256; i++) {
		c = i<<24;
		for (j=8; j; j--)
			c=c&0x80000000 ? (c<<1)^0x04c11db7 : (c<<1);
		bd->crc32Table[i] = c;
	}

	// Ensure that file starts with "BZh".
    for (i=0;i<3;i++)
		if (get_bits(bd,8)!="BZh"[i]) return RETVAL_NOT_BZIP_DATA;

	// Next byte ascii '1'-'9', indicates block size in units of 100k of
	// uncompressed data.  Allocate intermediate buffer for block.
	i = get_bits(bd, 8);
	if (i<'1' || i>'9') return RETVAL_NOT_BZIP_DATA;
	bd->dbufSize = 100000*(i-'0');
	bd->dbuf = xmalloc(bd->dbufSize * sizeof(int));

	return RETVAL_OK;
}

// Example usage: decompress src_fd to dst_fd.  (Stops at end of bzip data,
// not end of file.)
void bunzipStream(int src_fd, int dst_fd)
{
	bunzip_data *bd;
	int i;

	if (!(i = start_bunzip(&bd,src_fd,0,0))) {
		i = write_bunzip_data(bd,dst_fd,0,0);
		if (i==RETVAL_LAST_BLOCK && bd->headerCRC==bd->totalCRC) i = RETVAL_OK;
	}
	flush_bunzip_outbuf(bd,dst_fd);
	free(bd->dbuf);
	free(bd);
	if (i) error_exit(bunzip_errors[-i]);
}
