/* vi: set ts=4:
 *
 * mke2fs.c - Create an ext2 filesystem image.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

#define TT toy.mke2fs

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
	struct ext2_superblock *sb = xzalloc(sizeof(struct ext2_superblock));
	uint32_t temp;
	off_t length;

	// Handle command line arguments.

	if (toys.optargs[1]) {
		sscanf(toys.optargs[1], "%u", &TT.blocks);
		temp = O_RDWR|O_CREAT;
	} else temp = O_RDWR;

	// TODO: Check if filesystem is mounted here

	// For mke?fs, open file.  For gene?fs, create file.
	length = fdlength(TT.fsfd = xcreate(*toys.optargs, temp, 0777));

	// TODO: collect gene2fs list, calculate requirements.

	// Fill out superblock structure

	// Determine appropriate block size, set log_block_size and log_frag_size.

	if (!TT.blocksize) TT.blocksize = (length && length < 1<<29) ? 1024 : 4096;
	if (TT.blocksize == 1024) temp = 0;
	else if (TT.blocksize == 2048) temp = 1;
	else if (TT.blocksize == 4096) temp = 2;
	else error_exit("bad blocksize");
	sb->log_block_size = sb->log_frag_size = SWAP_LE32(temp);

	// Fill out blocks_count and r_blocks_count

	if (!TT.blocks) TT.blocks = length/TT.blocksize;
	sb->blocks_count = SWAP_LE32(TT.blocks);

	if (!TT.reserved_percent) TT.reserved_percent = 5;
	temp = (TT.blocks * (uint64_t)TT.reserved_percent) /100;
	sb->r_blocks_count = SWAP_LE32(temp);

	// Set blocks_per_group and frags_per_group, which is the size of an
	// allocation bitmap that fits in one block (I.E. how many bits per block)?

	temp = TT.blocksize*8;
	sb->blocks_per_group = sb->frags_per_group = SWAP_LE32(temp);

	// How many block groups do we need?  (Round up avoiding integer overflow.)

	TT.groups = (TT.blocks)/temp;
	if (TT.blocks & (temp-1)) TT.groups++;

	// Figure out how many inodes we need.

	if (!TT.inodes) {
		if (!TT.bytes_per_inode) TT.bytes_per_inode = 8192;
		TT.inodes = (TT.blocks * (uint64_t)TT.blocksize) / TT.bytes_per_inode;
	}

	// Figure out inodes per group, rounded up to block size.

	// How many blocks of inodes total, rounded up
	temp = TT.inodes / (TT.blocksize/sizeof(struct ext2_inode));
	if (TT.inodes & (TT.blocksize-1)) temp++;
	// How many blocks of inodes per group, again rounded up
	TT.inodes = temp / TT.groups;
	if (temp & (TT.groups-1)) TT.inodes++;
	// How many inodes per group is that?
	TT.inodes *=  (TT.blocksize/sizeof(struct ext2_inode));

	// Set inodes_per_group and total inodes_count
	sb->inodes_per_group = SWAP_LE32(TT.inodes);
	sb->inodes_count = SWAP_LE32(TT.inodes *= TT.groups);

	// Fill out the rest of the superblock.
	sb->max_mnt_count=0xFFFF;
	sb->wtime = sb->lastcheck = sb->mkfs_time = SWAP_LE32(time(NULL));
	sb->magic = SWAP_LE32(0xEF53);
	sb->state = sb->errors = SWAP_LE16(1);

	sb->rev_level = SWAP_LE32(1);
	sb->inode_size = sizeof(struct ext2_inode);
	sb->feature_incompat = SWAP_LE32(EXT2_FEATURE_INCOMPAT_FILETYPE);
	sb->feature_ro_compat = SWAP_LE32(EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);

	create_uuid(sb->uuid);

	// If we're called as mke3fs or mkfs.ext3, do a journal.

	//if (strchr(toys.which->name,'3'))
	//	sb->feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;

	// We skip the first 1k (to avoid the boot sector, if any).  Use this to
	// figure out if this file is seekable.
	if(-1 == lseek(TT.fsfd, 1024, SEEK_SET)) perror_exit("lseek");
	//{ TT.noseek=1; xwrite(TT.fsfd, sb, 1024); }

	// Write superblock to disk.	
	xwrite(TT.fsfd, sb, sizeof(struct ext2_superblock)); // 4096-1024

	return 0;
}
