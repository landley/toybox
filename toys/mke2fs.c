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

#define INODES_RESERVED 10

// Calculate data blocks plus index blocks needed to hold a file.

static uint32_t count_blocks_used(uint64_t size)
{
	uint32_t dblocks = (uint32_t)((size+(TT.blocksize-1))/TT.blocksize);
	uint32_t idx=TT.blocksize/4, iblocks=0, diblocks=0, tiblocks=0;

	// Account for direct, singly, doubly, and triply indirect index blocks

	if (dblocks > 12) {
		iblocks = ((dblocks-13)/idx)+1;
		if (iblocks > 1) {
			diblocks = ((iblocks-2)/idx)+1;
			if (diblocks > 1)
				tiblocks = ((diblocks-2)/idx)+1;
		}
	}

	return dblocks + iblocks + diblocks + tiblocks;
}

// Calculate the number of blocks used by each inode.  Returns blocks used,
// assigns bytes used to *size.  Writes total block count to TT.treeblocks
// and inode count to TT.treeinodes.

long check_treesize(struct dirtree *this, off_t *size)
{
	long blocks;

	while (this) {
		*size += sizeof(struct ext2_dentry) + strlen(this->name);

		if (this->child)
			this->st.st_blocks = check_treesize(this->child, &this->st.st_size);
		else if (S_ISREG(this->st.st_mode)) {
			 this->st.st_blocks = count_blocks_used(this->st.st_size);
			 TT.treeblocks += this->st.st_blocks;
		}
		this = this->next;
	}
	TT.treeblocks += blocks = count_blocks_used(*size);
	TT.treeinodes++;

	return blocks;
}

// Use the parent pointer to iterate through the tree non-recursively.
static struct dirtree *treenext(struct dirtree *this)
{
	while (this && !this->next) this = this->parent;
	if (this) this = this->next;

	return this;
}

// To do this right I need to copy the tree and sort it, but here's a really
// ugly n^2 way of dealing with the problem that doesn't scale well to large
// numbers of files but can be done in very little code.

static void check_treelinks(void)
{
	struct dirtree *this, *that;

	for (this = TT.dt; this; this = treenext(this)) {
		this->st.st_nlink = 0;
		for (that = TT.dt; that; that = treenext(that))
			if (this->st.st_ino == that->st.st_ino)
				if (this->st.st_dev == that->st.st_dev)
					this->st.st_nlink++;
	}
}

// According to http://www.opengroup.org/onlinepubs/9629399/apdxa.htm
// we should generate a uuid structure by reading a clock with 100 nanosecond
// precision, normalizing it to the start of the gregorian calendar in 1582,
// and looking up our eth0 mac address.
//
// On the other hand, we have 128 bits to come up with a unique identifier, of
// which 6 have a defined value.  /dev/urandom it is.

static void create_uuid(char *uuid)
{
	// Read 128 random bits
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

// Fill out superblock and TT

static void init_superblock(struct ext2_superblock *sb)
{
	uint32_t temp;

	// Set log_block_size and log_frag_size.

	for (temp = 0; temp < 4; temp++) if (TT.blocksize == 1024<<temp) break;
	if (temp==4) error_exit("bad blocksize");
	sb->log_block_size = sb->log_frag_size = SWAP_LE32(temp);

	// Fill out blocks_count, r_blocks_count, first_data_block

	sb->blocks_count = SWAP_LE32(TT.blocks);
	if (!TT.reserved_percent) TT.reserved_percent = 5;
	temp = (TT.blocks * (uint64_t)TT.reserved_percent) /100;
	sb->r_blocks_count = SWAP_LE32(temp);

	sb->first_data_block = SWAP_LE32(TT.blocksize == 1024 ? 1 : 0);

	// Set blocks_per_group and frags_per_group, which is the size of an
	// allocation bitmap that fits in one block (I.E. how many bits per block)?

	temp = TT.blocksize*8;
	sb->blocks_per_group = sb->frags_per_group = SWAP_LE32(temp);

	// How many block groups do we need?  (Round up avoiding integer overflow.)

	TT.groups = (TT.blocks)/temp;
	if (TT.blocks & (temp-1)) TT.groups++;

	// Figure out how many total inodes we need.

	if (!TT.inodespg) {
		if (!TT.bytes_per_inode) TT.bytes_per_inode = 8192;
		TT.inodespg = (TT.blocks * (uint64_t)TT.blocksize) / TT.bytes_per_inode;
	}

	// Figure out inodes per group, rounded up to block size.

	// How many blocks of inodes total, rounded up
	temp = TT.inodespg / (TT.blocksize/sizeof(struct ext2_inode));
	if (temp * (TT.blocksize/sizeof(struct ext2_inode)) != TT.inodespg) temp++;
	// How many blocks of inodes per group, again rounded up
	TT.inodespg = temp / TT.groups;
	if (temp & (TT.groups-1)) TT.inodespg++;
	// How many inodes per group is that?
	TT.inodespg *=  (TT.blocksize/sizeof(struct ext2_inode));

	// Set inodes_per_group and total inodes_count
	sb->inodes_per_group = SWAP_LE32(TT.inodespg);
	sb->inodes_count = SWAP_LE32(TT.inodespg * TT.groups);

	// Fill out the rest of the superblock.
	sb->max_mnt_count=0xFFFF;
	sb->wtime = sb->lastcheck = sb->mkfs_time = SWAP_LE32(time(NULL));
	sb->magic = SWAP_LE32(0xEF53);
	sb->state = sb->errors = SWAP_LE16(1);

	sb->rev_level = SWAP_LE32(1);
	sb->first_ino = SWAP_LE32(INODES_RESERVED+1);
	sb->inode_size = SWAP_LE16(sizeof(struct ext2_inode));
	sb->feature_incompat = SWAP_LE32(EXT2_FEATURE_INCOMPAT_FILETYPE);
	sb->feature_ro_compat = SWAP_LE32(EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);

	create_uuid(sb->uuid);

	// TODO If we're called as mke3fs or mkfs.ext3, do a journal.

	//if (strchr(toys.which->name,'3'))
	//	sb->feature_compat |= SWAP_LE32(EXT3_FEATURE_COMPAT_HAS_JOURNAL);
}

// Number of blocks used in this group by superblock/group list backup.
// Returns 0 if this group doesn't have a superblock backup.
static int group_superblock_used(uint32_t group)
{
	int used = 0, i;

	// Superblock backups are on groups 0, 1, and powers of 3, 5, and 7.
	if(!group || group==1) used++;
	for (i=3; i<9; i+=2) {
		int j = i;
		while (j<group) j*=i;
		if (j==group) used++;
	}

	if (used) {
		// How blocks does the group table take up?
		used = TT.groups * sizeof(struct ext2_group);
		used += TT.blocksize - 1;
		used /= TT.blocksize;
		// Plus the superblock itself.
		used++;
		// And a corner case.
		if (!group && TT.blocksize == 1024) used++;
	}

	return used;
}

static void bits_set(char *array, int start, int len)
{
	while(len) {
		if ((start&7) || len<8) {
			array[start/8]|=(1<<(start&7));
			start++;
			len--;
		} else {
			array[start/8]=255;
			start+=8;
			len-=8;
		}
	}
}

// Seek past len bytes (to maintain sparse file), or write zeroes if output
// not seekable
static void put_zeroes(int len)
{
	if(TT.noseek || -1 == lseek(TT.fsfd, len, SEEK_SET)) {

		TT.noseek=1;
		memset(toybuf, 0, sizeof(toybuf));
		while (len) {
			int out = len > sizeof(toybuf) ? sizeof(toybuf) : len;
			xwrite(TT.fsfd, toybuf, out);
			len -= out;
		}
	}
}

static void fill_inode(struct ext2_inode *in, struct dirtree *this)
{
	memset(in,0,sizeof(struct ext2_inode));

	// This works on Linux.  S_ISREG/DIR/CHR/BLK/FIFO/LNK/SOCK(m)
	in->mode = this->st.st_mode;

	in->uid = this->st.st_uid & 0xFFFF;
	in->uid_high = this->st.st_uid >> 16;
	in->gid = this->st.st_gid & 0xFFFF;
	in->gid_high = this->st.st_gid >> 16;
	in->size = this->st.st_size & 0xFFFFFFFF;

	in->atime = this->st.st_atime;
	in->ctime = this->st.st_ctime;
	in->mtime = this->st.st_mtime;

	in->links_count = this->st.st_nlink;
	in->blocks = this->st.st_blocks;
}

int mke2fs_main(void)
{
	int i, temp, blockbits;
	off_t length;

	// Handle command line arguments.

	if (toys.optargs[1]) {
		sscanf(toys.optargs[1], "%u", &TT.blocks);
		temp = O_RDWR|O_CREAT;
	} else temp = O_RDWR;

	// Collect gene2fs list or lost+found, calculate requirements.

	if (TT.gendir) {
		strncpy(toybuf, TT.gendir, sizeof(toybuf));
		TT.dt = read_dirtree(toybuf, NULL);
	} else {
		TT.dt = xzalloc(sizeof(struct dirtree)+11);
		strcpy(TT.dt->name, "lost+found");
		TT.dt->st.st_mode = S_IFDIR|0755;
		TT.dt->st.st_ctime = TT.dt->st.st_mtime = time(NULL);
	}

	// TODO: Check if filesystem is mounted here

	// For mke?fs, open file.  For gene?fs, create file.
	TT.fsfd = xcreate(*toys.optargs, temp, 0777);

	// Determine appropriate block size and block count from file length.

	length = fdlength(TT.fsfd);
	if (!TT.blocksize) TT.blocksize = (length && length < 1<<29) ? 1024 : 4096;
	if (!TT.blocks) TT.blocks = length/TT.blocksize;
	if (!TT.blocks) error_exit("gene2fs is a TODO item");

	// Skip the first 1k to avoid the boot sector (if any), then
	// initialize superblock structure

	put_zeroes(1024);
	init_superblock(&TT.sb);
	blockbits = 8*TT.blocksize;

	// Figure out how much space is used
	length = 0;
	length = check_treesize(TT.dt, &length);
	for (temp=i=0; i<TT.groups; i++) {
		temp += group_superblock_used(i) + 2;
		temp += TT.inodespg/(TT.blocksize/sizeof(struct ext2_inode));
	}
	TT.sb.free_blocks_count = SWAP_LE32(TT.blocks - TT.treeblocks - temp);
	TT.sb.free_inodes_count = SWAP_LE32(TT.inodespg*TT.groups - INODES_RESERVED
					- TT.treeinodes);

	// Calculate st_nlink for each node in tree.

	check_treelinks();

	// Loop through block groups.

	for (i=0; i<TT.groups; i++) {
		struct ext2_inode *in = (struct ext2_inode *)toybuf;
		uint32_t start, itable, used, end;
		int j, slot;

		// Where does this group end?
		end = blockbits;
		if ((i+1)*blockbits > TT.blocks) end = TT.blocks & (blockbits-1);

		// Blocks used by inode table
		itable = (TT.inodespg*sizeof(struct ext2_inode))/TT.blocksize;

		// If a superblock goes here, write it out.
		start = group_superblock_used(i);
		if (start) {
			struct ext2_group *bg = (struct ext2_group *)toybuf;

			TT.sb.block_group_nr = SWAP_LE16(i);

			// Write superblock and pad it up to block size
			xwrite(TT.fsfd, &TT.sb, sizeof(struct ext2_superblock));
			temp = TT.blocksize - sizeof(struct ext2_superblock);
			if (!i && TT.blocksize > 1024) temp -= 1024;
			memset(toybuf, 0, TT.blocksize);
			xwrite(TT.fsfd, toybuf, temp);

			// Loop through groups to write group descriptor table.
			for(j=0; j<TT.groups; j++) {

				// Figure out what sector this group starts in.
				used = group_superblock_used(j);

				// Find next array slot in this block (flush block if full).
				slot = j % (TT.blocksize/sizeof(struct ext2_group));
				if (!slot) {
					if (j) xwrite(TT.fsfd, bg, TT.blocksize);
					memset(bg, 0, TT.blocksize);
				}

				// How many free inodes in this group?  (TODO)
				temp = TT.inodespg;
				if (!i) temp -= INODES_RESERVED;
				bg[slot].free_inodes_count = SWAP_LE16(temp);

				// How many free blocks in this group?  (TODO)
				temp = TT.inodespg/(TT.blocksize/sizeof(struct ext2_inode)) + 2;
				temp = end-used-temp;
				bg[slot].free_blocks_count = SWAP_LE32(temp);

				// Fill out rest of group structure (TODO: gene2fs allocation)
				used += j*blockbits;
				bg[slot].block_bitmap = SWAP_LE32(used++);
				bg[slot].inode_bitmap = SWAP_LE32(used++);
				bg[slot].inode_table = SWAP_LE32(used);
				bg[slot].used_dirs_count = 0;  // (TODO)
			}
			xwrite(TT.fsfd, bg, TT.blocksize);
		}

		// Now write out stuff that every block group has.

		// Write block usage bitmap  (TODO: fill it)

		memset(toybuf, 0, TT.blocksize);
		bits_set(toybuf, 0, start+itable);
		if (end!=blockbits) bits_set(toybuf, end, blockbits-end);
		xwrite(TT.fsfd, toybuf, TT.blocksize);

		// Write inode bitmap  (TODO)
		memset(toybuf, 0, TT.blocksize);
		if (!i) bits_set(toybuf, 0, INODES_RESERVED);
		bits_set(toybuf, TT.inodespg, blockbits-TT.inodespg);
		xwrite(TT.fsfd, toybuf, TT.blocksize);

		start += 3;

		// Write inode table for this group
		for (j = 0; j<TT.inodespg; j++) {
			slot = j % (TT.blocksize/sizeof(struct ext2_inode));
			if (!slot) {
				if (j) {
					xwrite(TT.fsfd, in, TT.blocksize);
					start++;
				}
				memset(in, 0, TT.blocksize);
			}
		}
		xwrite(TT.fsfd, in, TT.blocksize);

		// Write empty data blocks
		put_zeroes((end-start) * TT.blocksize);
	}

	return 0;
}
