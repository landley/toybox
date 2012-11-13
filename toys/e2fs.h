/* mke2fs.h - Headers for ext2
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

// Stuff defined in linux/ext2_fs.h

#define EXT2_SUPER_MAGIC  0xEF53

struct ext2_superblock {
  uint32_t inodes_count;      // Inodes count
  uint32_t blocks_count;      // Blocks count
  uint32_t r_blocks_count;    // Reserved blocks count
  uint32_t free_blocks_count; // Free blocks count
  uint32_t free_inodes_count; // Free inodes count
  uint32_t first_data_block;  // First Data Block
  uint32_t log_block_size;    // Block size
  uint32_t log_frag_size;     // Fragment size
  uint32_t blocks_per_group;  // Blocks per group
  uint32_t frags_per_group;   // Fragments per group
  uint32_t inodes_per_group;  // Inodes per group
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
  uint32_t mkfs_time;          // Creation timestamp
  uint32_t jnl_blocks[17];     // Backup of journal inode
  // uint32_t reserved[172];      // Padding to the end of the block
};

struct ext2_group
{
  uint32_t block_bitmap;       // Block number of block bitmap
  uint32_t inode_bitmap;       // Block number of inode bitmap
  uint32_t inode_table;        // Block number of inode table
  uint16_t free_blocks_count;  // How many free blocks in this group?
  uint16_t free_inodes_count;  // How many free inodes in this group?
  uint16_t used_dirs_count;    // How many directories?
  uint16_t reserved[7];        // pad to 32 bytes
};

struct ext2_dentry {
  uint32_t inode;         // Inode number
  uint16_t rec_len;       // Directory entry length
  uint8_t  name_len;      // Name length
  uint8_t  file_type;
  char     name[0];     // File name
};

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
  uint32_t dir_acl;     // Directory ACL (or top bits of file length)
  uint32_t faddr;       // Last block in file
  uint8_t  frag;        // Fragment number
  uint8_t  fsize;       // Fragment size
  uint16_t pad1;
  uint16_t uid_high;    // High bits of uid
  uint16_t gid_high;    // High bits of gid
  uint32_t reserved2;
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
