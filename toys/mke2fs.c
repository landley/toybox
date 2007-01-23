/* vi: set ts=4:
 *
 * mke2fs.c - Create an ext2 filesystem image.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

	// b - block size (1024, 2048, 4096)
	// F - force (run on mounted device or non-block device)
	// i - bytes per inode 
	// N - number of inodes
	// m - reserved blocks percentage
	// n - Don't write
	// q - quiet

	// L - volume label
	// M - last mounted path
	// o - creator os
	
	// j - create journal
	// J - journal options (size=1024-102400 blocks,device=)
	//        device=/dev/blah or LABEL=label UUID=uuid

	// E - extended options (stride=stripe-size blocks)
	// O - none,dir_index,filetype,has_journal,journal_dev,sparse_super


// According to http://www.opengroup.org/onlinepubs/9629399/apdxa.htm
// we should generate a uuid structure by reading a clock with 100 nanosecond
// precision, normalizing it to the start of the gregorian calendar in 1582,
// and looking up our eth0 mac address.
//
// On the other hand, we have 128 bits to come up with a unique identifier, of
// which 6 have a defined value.  /dev/urandom it is.

void create_uuid(char *uuid)
{
	// Read 128 random bytes
	int fd = xopen("/dev/urandom", O_RDONLY);
	xreadall(fd, uuid, 16);
	close(fd);

	// Claim to be a DCE format UUID.
	uuid[6] = (uuid[6] & 0x0F) | 0x40;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

    // rfc2518 section 6.4.1 suggests if we're not using a macaddr, we should
	// set bit 1 of the node ID, which is the mac multicast bit.  This means we
	// should never collide with anybody actually using a macaddr.
	uuid[11] = uuid[11] | 128;
}

int mke2fs_main(void)
{
	struct ext2_super_block *sb = xzalloc(sizeof(struct ext2_super_block));
	int temp;
	off_t length;

	// Handle command line arguments.

	if (toys.optargs[1]) {
		sscanf(toys.optargs[1], "%u", &(sb->inodes_count));
		temp = O_RDWR|O_CREAT;
	} else temp = O_RDWR;

	// Check if filesystem is mounted

	// For mke?fs, open file.  For gene?fs, create file.
	length = fdlength(toy.mke2fs.fsfd = xcreate(*toys.optargs, temp, 0777));

	if (toy.mke2fs.blocksize && toy.mke2fs.blocksize!=1024
		&& toy.mke2fs.blocksize!=2048 && toy.mke2fs.blocksize!=4096)
			error_exit("bad blocksize");

	// Determine block size.  If unspecified, use simple heuristic.
	if (toy.mke2fs.blocksize) 
		sb->log_block_size = (length && length < 1<<24) ? 1024 : 4096;
	else sb->log_block_size = toy.mke2fs.blocksize;

	if (!sb->inodes_count) sb->inodes_count = length/toy.mke2fs.blocksize;

	// Fill out superblock structure

	sb->rev_level = SWAP_LE32(1);
	sb->feature_incompat = SWAP_LE32(EXT2_FEATURE_INCOMPAT_FILETYPE);
	sb->feature_ro_compat = SWAP_LE32(EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);

	// If we're called as mke3fs or mkfs.ext3, do a journal.

	//if (strchr(toys.which->name,'3'))
	//	sb->feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;

	// We skip the first 1k (to avoid the boot sector, if any).  Use this to
	// figure out if this file is seekable.
	if(-1 == lseek(toy.mke2fs.fsfd, 1024, SEEK_SET)) perror_exit("lseek");
	//{ toy.mke2fs.noseek=1; xwrite(toy.mke2fs.fsfd, sb, 1024); }

	// Write superblock to disk.	
	xwrite(toy.mke2fs.fsfd, sb, 3072); // 4096-1024

	return 0;
}
