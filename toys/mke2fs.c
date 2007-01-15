/* vi: set ts=4:
 *
 * mke2fs.c - Create an ext2 filesystem image.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Stuff defined in linux/ext2_fs.h

#define EXT2_SUPER_MAGIC  0xEF53

struct ext2_inode {
	uint16_t mode;        // File mode
	uint16_t uid;         // Low 16 bits of Owner Uid
	uint32_t size;        // Size in bytes
	uint32_t atime;       // Access time
	uint32_t ctime;       // Creation time
	uint32_t mtime;       // Modification time
	uint32_t dtime;       // Deletion Time
	uint16_t gid;         // Low 16 bits of Group Id
	uint16_t links_count; // Links count
	uint32_t blocks;      // Blocks count
	uint32_t flags;       // File flags
	uint32_t reserved1;
	uint32_t block[15];   // Pointers to blocks
	uint32_t generation;  // File version (for NFS)
	uint32_t file_acl;    // File ACL
	uint32_t dir_acl;     // Directory ACL
	uint32_t faddr;       // Fragment address
	uint8_t  frag;        // Fragment number
	uint8_t  fsize;       // Fragment size
	uint16_t pad1;
	uint16_t uid_high;    // High bits of uid
	uint16_t gid_high;    // High bits of gid
	uint32_t reserved2;
};

struct ext2_super_block {
	uint32_t inodes_count;      // Inodes count
	uint32_t blocks_count;      // Blocks count
	uint32_t r_blocks_count;    // Reserved blocks count
	uint32_t free_blocks_count; // Free blocks count
	uint32_t free_inodes_count; // Free inodes count
	uint32_t first_data_block;  // First Data Block
	uint32_t log_block_size;    // Block size
	uint32_t log_frag_size;     // Fragment size
	uint32_t blocks_per_group;  // # Blocks per group
	uint32_t frags_per_group;   // # Fragments per group
	uint32_t inodes_per_group;  // # Inodes per group
	uint32_t mtime;             // Mount time
	uint32_t wtime;             // Write time
	uint16_t mnt_count;         // Mount count
	uint16_t max_mnt_count;     // Maximal mount count
	uint16_t magic;             // Magic signature
	uint16_t state;             // File system state
	uint16_t errors;            // Behaviour when detecting errors
	uint16_t minor_rev_level;   // minor revision level
	uint32_t lastcheck;         // time of last check
	uint32_t checkinterval;     // max. time between checks
	uint32_t creator_os;        // OS
	uint32_t rev_level;         // Revision level
	uint16_t def_resuid;        // Default uid for reserved blocks
	uint16_t def_resgid;        // Default gid for reserved blocks
	uint32_t first_ino;         // First non-reserved inode
	uint16_t inode_size;        // size of inode structure
	uint16_t block_group_nr;    // block group # of this superblock
	uint32_t feature_compat;    // compatible feature set
	uint32_t feature_incompat;  // incompatible feature set
	uint32_t feature_ro_compat; // readonly-compatible feature set
	char     uuid[16];          // 128-bit uuid for volume
	char     volume_name[16];   // volume name
	char     last_mounted[64];  // directory where last mounted
	uint32_t alg_usage_bitmap;  // For compression
	// For EXT2_COMPAT_PREALLOC
	uint8_t  prealloc_blocks;   // Nr of blocks to try to preallocate
	uint8_t  prealloc_dir_blocks; //Nr to preallocate for dirs
	uint16_t padding1;
	// For EXT3_FEATURE_COMPAT_HAS_JOURNAL
	uint8_t  journal_uuid[16];   // uuid of journal superblock
	uint32_t journal_inum;       // inode number of journal file
	uint32_t journal_dev;        // device number of journal file
	uint32_t last_orphan;        // start of list of inodes to delete
	uint32_t hash_seed[4];       // HTREE hash seed
	uint8_t  def_hash_version;   // Default hash version to use
	uint8_t  padding2[3];
	uint32_t default_mount_opts;
 	uint32_t first_meta_bg;      // First metablock block group
	uint32_t reserved[190];      // Padding to the end of the block
};

#define EXT2_FEATURE_COMPAT_DIR_PREALLOC	0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES	0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL		0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR		0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INO		0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX		0x0020

#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE	0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR	0x0004

#define EXT2_FEATURE_INCOMPAT_COMPRESSION	0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER		0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG		0x0010

#define EXT2_NAME_LEN 255

struct ext2_dir_entry_2 {
	uint32_t inode;         // Inode number
    uint16_t rec_len;       // Directory entry length
    uint8_t  name_len;      // Name length
    uint8_t  file_type;
	char     name[255];     // File name
};

// Ext2 directory file types.  Only the low 3 bits are used.  The
// other bits are reserved for now.

enum {
	EXT2_FT_UNKNOWN,
	EXT2_FT_REG_FILE,
	EXT2_FT_DIR,
	EXT2_FT_CHRDEV,
	EXT2_FT_BLKDEV,
	EXT2_FT_FIFO,
	EXT2_FT_SOCK,
	EXT2_FT_SYMLINK,
	EXT2_FT_MAX
};


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


// This is what's in a UUID according to the spec at
// http://www.opengroup.org/onlinepubs/9629399/apdxa.htm

//struct uuid {
//	uint32_t  time_low;
//	uint16_t  time_mid;
//	uint16_t  time_hi_and_version;
//	uint8_t   clock_seq_hi_and_reserved;
//	uint8_t   clock_seq_low;
//	uint8_t   node[6];
//};


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

	// Handle command line arguments.

	if (!*toys.optargs || (!CFG_MKE2FS_GEN && toys.optargs[1])) usage_exit();
	if (CFG_MKE2FS_GEN && toys.optargs[1]) {
			temp = O_RDWR|O_CREAT;
			xaccess(toys.optargs[1], R_OK);
	} else temp = O_RDWR;
	if (toy.mke2fs.blocksize!=1024 && toy.mke2fs.blocksize!=2048
		&& toy.mke2fs.blocksize!=4096) error_exit("bad blocksize");

	// For mke?fs, open file.  For gene?fs, create file.
	toy.mke2fs.fsfd = xcreate(*toys.optargs, temp, 0777);

    // We don't autodetect block size from external journaling devices, instead
	// we write our block size to that journaling device.  (If they want a
	// specific block size, they have the -b option.)

// What's the deal with fs_type?
// line 1059

	// We skip the first 1k (to avoid the boot sector, if any).  Use this to
	// figure out if this file is seekable.
	if(-1 == lseek(toy.mke2fs.fsfd, 1024, SEEK_SET)) {
		toy.mke2fs.noseek=1;
		xwrite(toy.mke2fs.fsfd, sb, 1024);
	}

	// Fill out superblock structure

	sb->rev_level = SWAP_LE32(1);
	sb->feature_incompat = SWAP_LE32(EXT2_FEATURE_INCOMPAT_FILETYPE);
	sb->feature_ro_compat = SWAP_LE32(EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);

	// If we're called as mke3fs or mkfs.ext3, do a journal.

	if (strchr(toys.which->name,'3'))
		sb->feature_compat |= EXT3_FEATURE_COMPAT_HAS_JOURNAL;

	// Write superblock to disk.	
	xwrite(toy.mke2fs.fsfd, sb, 3072); // 4096-1024

	return 0;
}
