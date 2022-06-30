/* mke2fs.c - Create an ext2 filesystem image.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>

// Still to go: "E:jJ:L:m:O:"
USE_MKE2FS(NEWTOY(mke2fs, "<1>2g:Fnqm#N#i#b#", TOYFLAG_SBIN))

config MKE2FS
  bool "mke2fs"
  default n
  help
    usage: mke2fs [-Fnq] [-b ###] [-N|i ###] [-m ###] device

    Create an ext2 filesystem on a block device or filesystem image.

    -F         Force to run on a mounted device
    -n         Don't write to device
    -q         Quiet (no output)
    -b size    Block size (1024, 2048, or 4096)
    -N inodes  Allocate this many inodes
    -i bytes   Allocate one inode for every XXX bytes of device
    -m percent Reserve this percent of filesystem space for root user

config MKE2FS_JOURNAL
  bool "Journaling support (ext3)"
  default n
  depends on MKE2FS
  help
    usage: mke2fs [-j] [-J size=###,device=XXX]

    -j         Create journal (ext3)
    -J         Journal options
               size: Number of blocks (1024-102400)
               device: Specify an external journal

config MKE2FS_GEN
  bool "Generate (gene2fs)"
  default n
  depends on MKE2FS
  help
    usage: gene2fs [options] device filename

    The [options] are the same as mke2fs.

config MKE2FS_LABEL
  bool "Label support"
  default n
  depends on MKE2FS
  help
    usage: mke2fs [-L label] [-M path] [-o string]

    -L         Volume label
    -M         Path to mount point
    -o         Created by

config MKE2FS_EXTENDED
  bool "Extended options"
  default n
  depends on MKE2FS
  help
    usage: mke2fs [-E stride=###] [-O option[,option]]

    -E stride= Set RAID stripe size (in blocks)
    -O [opts]  Specify fewer ext2 option flags (for old kernels)
               All of these are on by default (as appropriate)
       none         Clear default options (all but journaling)
       dir_index    Use htree indexes for large directories
       filetype     Store file type info in directory entry
       has_journal  Set by -j
       journal_dev  Set by -J device=XXX
       sparse_super Don't allocate huge numbers of redundant superblocks
*/

#define FOR_mke2fs
#include "toys.h"

GLOBALS(
  // Command line arguments.
  long blocksize;
  long bytes_per_inode;
  long inodes;           // Total inodes in filesystem.
  long reserved_percent; // Integer precent of space to reserve for root.
  char *gendir;          // Where to read dirtree from.

  // Internal data.
  struct dirtree *dt;    // Tree of files to copy into the new filesystem.
  unsigned treeblocks;   // Blocks used by dt
  unsigned treeinodes;   // Inodes used by dt

  unsigned blocks;       // Total blocks in the filesystem.
  unsigned freeblocks;   // Free blocks in the filesystem.
  unsigned inodespg;     // Inodes per group
  unsigned groups;       // Total number of block groups.
  unsigned blockbits;    // Bits per block.  (Also blocks per group.)

  // For gene2fs
  unsigned nextblock;    // Next data block to allocate
  unsigned nextgroup;    // Next group we'll be allocating from
  int fsfd;              // File descriptor of filesystem (to output to).
)

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

#define INODES_RESERVED 10

static uint32_t div_round_up(uint32_t a, uint32_t b)
{
  uint32_t c = a/b;

  if (a%b) c++;
  return c;
}

// Calculate data blocks plus index blocks needed to hold a file.

static uint32_t file_blocks_used(uint64_t size, uint32_t *blocklist)
{
  uint32_t dblocks = (uint32_t)((size+(TT.blocksize-1))/TT.blocksize);
  uint32_t idx=TT.blocksize/4, iblocks=0, diblocks=0, tiblocks=0;

  // Fill out index blocks in inode.

  if (blocklist) {
    int i;

    // Direct index blocks
    for (i=0; i<13 && i<dblocks; i++) blocklist[i] = i;
    // Singly indirect index blocks
    if (dblocks > 13+idx) blocklist[13] = 13+idx;
    // Doubly indirect index blocks
    idx = 13 + idx + (idx*idx);
    if (dblocks > idx) blocklist[14] = idx;

    return 0;
  }

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

// Use the parent pointer to iterate through the tree non-recursively.
static struct dirtree *treenext(struct dirtree *this)
{
  while (this && !this->next) this = this->parent;
  if (this) this = this->next;

  return this;
}

// Recursively calculate the number of blocks used by each inode in the tree.
// Returns blocks used by this directory, assigns bytes used to *size.
// Writes total block count to TT.treeblocks and inode count to TT.treeinodes.

static long check_treesize(struct dirtree *that, off_t *size)
{
  long blocks;

  while (that) {
    *size += sizeof(struct ext2_dentry) + strlen(that->name);

    if (that->child)
      that->st.st_blocks = check_treesize(that->child, &that->st.st_size);
    else if (S_ISREG(that->st.st_mode)) {
       that->st.st_blocks = file_blocks_used(that->st.st_size, 0);
       TT.treeblocks += that->st.st_blocks;
    }
    that = that->next;
  }
  TT.treeblocks += blocks = file_blocks_used(*size, 0);
  TT.treeinodes++;

  return blocks;
}

// Calculate inode numbers and link counts.
//
// To do this right I need to copy the tree and sort it, but here's a really
// ugly n^2 way of dealing with the problem that doesn't scale well to large
// numbers of files (> 100,000) but can be done in very little code.
// This rewrites inode numbers to their final values, allocating depth first.

static void check_treelinks(struct dirtree *tree)
{
  struct dirtree *current=tree, *that;
  long inode = INODES_RESERVED;

  while (current) {
    ++inode;
    // Since we can't hardlink to directories, we know their link count.
    if (S_ISDIR(current->st.st_mode)) current->st.st_nlink = 2;
    else {
      dev_t new = current->st.st_dev;

      if (!new) continue;

      // Look for other copies of current node
      current->st.st_nlink = 0;
      for (that = tree; that; that = treenext(that)) {
        if (same_file(current, that)) {
          current->st.st_nlink++;
          current->st.st_ino = inode;
        }
      }
    }
    current->st.st_ino = inode;
    current = treenext(current);
  }
}

// Calculate inodes per group from total inodes.
static uint32_t get_inodespg(uint32_t inodes)
{
  uint32_t temp;

  // Round up to fill complete inode blocks.
  temp = (inodes + TT.groups - 1) / TT.groups;
  inodes = TT.blocksize/sizeof(struct ext2_inode);
  return ((temp + inodes - 1)/inodes)*inodes;
}

// Fill out superblock and TT structures.

static void init_superblock(struct ext2_superblock *sb)
{
  uint32_t temp;

  // Set log_block_size and log_frag_size.

  for (temp = 0; temp < 4; temp++) if (TT.blocksize == 1024<<temp) break;
  if (temp==4) error_exit("bad blocksize");
  sb->log_block_size = sb->log_frag_size = SWAP_LE32(temp);

  // Fill out blocks_count, r_blocks_count, first_data_block

  sb->blocks_count = SWAP_LE32(TT.blocks);
  sb->free_blocks_count = SWAP_LE32(TT.freeblocks);
  temp = (TT.blocks * (uint64_t)TT.reserved_percent) / 100;
  sb->r_blocks_count = SWAP_LE32(temp);

  sb->first_data_block = SWAP_LE32(TT.blocksize == 1024 ? 1 : 0);

  // Set blocks_per_group and frags_per_group, which is the size of an
  // allocation bitmap that fits in one block (I.E. how many bits per block)?

  sb->blocks_per_group = sb->frags_per_group = SWAP_LE32(TT.blockbits);

  // Set inodes_per_group and total inodes_count
  sb->inodes_per_group = SWAP_LE32(TT.inodespg);
  sb->inodes_count = SWAP_LE32(TT.inodespg * TT.groups);

  // Determine free inodes.
  temp = TT.inodespg*TT.groups - INODES_RESERVED;
  if (temp < TT.treeinodes) error_exit("Not enough inodes.\n");
  sb->free_inodes_count = SWAP_LE32(temp - TT.treeinodes);

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

// Does this group contain a superblock backup (and group descriptor table)?
static int is_sb_group(uint32_t group)
{
  int i;

  // Superblock backups are on groups 0, 1, and powers of 3, 5, and 7.
  if(!group || group==1) return 1;
  for (i=3; i<9; i+=2) {
    int j = i;
    while (j<group) j*=i;
    if (j==group) return 1;
  }
  return 0;
}


// Number of blocks used in group by optional superblock/group list backup.
static int group_superblock_overhead(uint32_t group)
{
  int used;

  if (!is_sb_group(group)) return 0;

  // How many blocks does the group descriptor table take up?
  used = TT.groups * sizeof(struct ext2_group);
  used += TT.blocksize - 1;
  used /= TT.blocksize;
  // Plus the superblock itself.
  used++;
  // And a corner case.
  if (!group && TT.blocksize == 1024) used++;

  return used;
}

// Number of blocks used in group to store superblock/group/inode list
static int group_overhead(uint32_t group)
{
  // Return superblock backup overhead (if any), plus block/inode
  // allocation bitmaps, plus inode tables.
  return group_superblock_overhead(group) + 2 + get_inodespg(TT.inodespg)
        / (TT.blocksize/sizeof(struct ext2_inode));
}

// In bitmap "array" set "len" bits starting at position "start" (from 0).
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
  if(-1 == lseek(TT.fsfd, len, SEEK_SET)) {
    memset(toybuf, 0, sizeof(toybuf));
    while (len) {
      int out = len > sizeof(toybuf) ? sizeof(toybuf) : len;
      xwrite(TT.fsfd, toybuf, out);
      len -= out;
    }
  }
}

// Fill out an inode structure from struct stat info in dirtree.
static void fill_inode(struct ext2_inode *in, struct dirtree *that)
{
  uint32_t fbu[15];
  int temp;

  file_blocks_used(that->st.st_size, fbu);

  // If that inode needs data blocks allocated to it.
  if (that->st.st_size) {
    int i, group = TT.nextblock/TT.blockbits;

    // TODO: teach this about indirect blocks.
    for (i=0; i<15; i++) {
      // If we just jumped into a new group, skip group overhead blocks.
      while (group >= TT.nextgroup)
        TT.nextblock += group_overhead(TT.nextgroup++);
    }
  }
  // TODO :  S_ISREG/DIR/CHR/BLK/FIFO/LNK/SOCK(m)
  in->mode = SWAP_LE32(that->st.st_mode);

  in->uid = SWAP_LE16(that->st.st_uid & 0xFFFF);
  in->uid_high = SWAP_LE16(that->st.st_uid >> 16);
  in->gid = SWAP_LE16(that->st.st_gid & 0xFFFF);
  in->gid_high = SWAP_LE16(that->st.st_gid >> 16);
  in->size = SWAP_LE32(that->st.st_size & 0xFFFFFFFF);

  // Contortions to make the compiler not generate a warning for x>>32
  // when x is 32 bits.  The optimizer should clean this up.
  if (sizeof(that->st.st_size) > 4) temp = 32;
  else temp = 0;
  if (temp) in->dir_acl = SWAP_LE32(that->st.st_size >> temp);

  in->atime = SWAP_LE32(that->st.st_atime);
  in->ctime = SWAP_LE32(that->st.st_ctime);
  in->mtime = SWAP_LE32(that->st.st_mtime);

  in->links_count = SWAP_LE16(that->st.st_nlink);
  in->blocks = SWAP_LE32(that->st.st_blocks);
  // in->faddr
}

// Works like an archiver.
// The first argument is the name of the file to create.  If it already
// exists, that size will be used.

void mke2fs_main(void)
{
  int i, temp;
  off_t length;
  uint32_t usedblocks, usedinodes, dtbblk;
  struct dirtree *dti, *dtb;
  struct ext2_superblock sb;

  // Handle command line arguments.

  if (toys.optargs[1]) {
    sscanf(toys.optargs[1], "%u", &TT.blocks);
    temp = O_RDWR|O_CREAT;
  } else temp = O_RDWR;
  if (!TT.reserved_percent) TT.reserved_percent = 5;

  // TODO: Check if filesystem is mounted here

  // For mke?fs, open file.  For gene?fs, create file.
  TT.fsfd = xcreate(*toys.optargs, temp, 0777);

  // Determine appropriate block size and block count from file length.
  // (If no length, default to 4k.  They can override it on the cmdline.)

  length = fdlength(TT.fsfd);
  if (!TT.blocksize) TT.blocksize = (length && length < 1<<29) ? 1024 : 4096;
  TT.blockbits = 8*TT.blocksize;
  if (!TT.blocks) TT.blocks = length/TT.blocksize;

  // Collect gene2fs list or lost+found, calculate requirements.

  if (TT.gendir) {
    strncpy(toybuf, TT.gendir, sizeof(toybuf));
    dti = dirtree_read(toybuf, dirtree_notdotdot);
  } else {
    dti = xzalloc(sizeof(struct dirtree)+11);
    strcpy(dti->name, "lost+found");
    dti->st.st_mode = S_IFDIR|0755;
    dti->st.st_ctime = dti->st.st_mtime = time(NULL);
  }

  // Add root directory inode.  This is iterated through for when finding
  // blocks, but not when finding inodes.  The tree's parent pointers don't
  // point back into this.

  dtb = xzalloc(sizeof(struct dirtree)+1);
  dtb->st.st_mode = S_IFDIR|0755;
  dtb->st.st_ctime = dtb->st.st_mtime = time(NULL);
  dtb->child = dti;

  // Figure out how much space is used by preset files
  length = check_treesize(dtb, &(dtb->st.st_size));
  check_treelinks(dtb);

  // Figure out how many total inodes we need.

  if (!TT.inodes) {
    if (!TT.bytes_per_inode) TT.bytes_per_inode = 8192;
    TT.inodes = (TT.blocks * (uint64_t)TT.blocksize) / TT.bytes_per_inode;
  }

  // If we're generating a filesystem and have no idea how many blocks it
  // needs, start with a minimal guess, find the overhead of that many
  // groups, and loop until this is enough groups to store this many blocks.
  if (!TT.blocks) TT.groups = (TT.treeblocks/TT.blockbits)+1;
  else TT.groups = div_round_up(TT.blocks, TT.blockbits);

  for (;;) {
    temp = TT.treeblocks;

    for (i = 0; i<TT.groups; i++) temp += group_overhead(i);

    if (TT.blocks) {
      if (TT.blocks < temp) error_exit("Not enough space.\n");
      break;
    }
    if (temp <= TT.groups * TT.blockbits) {
      TT.blocks = temp;
      break;
    }
    TT.groups++;
  }
  TT.freeblocks = TT.blocks - temp;

  // Now we know all the TT data, initialize superblock structure.

  init_superblock(&sb);

  // Start writing.  Skip the first 1k to avoid the boot sector (if any).
  put_zeroes(1024);

  // Loop through block groups, write out each one.
  dtbblk = usedblocks = usedinodes = 0;
  for (i=0; i<TT.groups; i++) {
    struct ext2_inode *in = (struct ext2_inode *)toybuf;
    uint32_t start, itable, used, end;
    int j, slot;

    // Where does this group end?
    end = TT.blockbits;
    if ((i+1)*TT.blockbits > TT.blocks) end = TT.blocks & (TT.blockbits-1);

    // Blocks used by inode table
    itable = (TT.inodespg*sizeof(struct ext2_inode))/TT.blocksize;

    // If a superblock goes here, write it out.
    start = group_superblock_overhead(i);
    if (start) {
      struct ext2_group *bg = (struct ext2_group *)toybuf;
      int treeblocks = TT.treeblocks, treeinodes = TT.treeinodes;

      sb.block_group_nr = SWAP_LE16(i);

      // Write superblock and pad it up to block size
      xwrite(TT.fsfd, &sb, sizeof(struct ext2_superblock));
      temp = TT.blocksize - sizeof(struct ext2_superblock);
      if (!i && TT.blocksize > 1024) temp -= 1024;
      memset(toybuf, 0, TT.blocksize);
      xwrite(TT.fsfd, toybuf, temp);

      // Loop through groups to write group descriptor table.
      for(j=0; j<TT.groups; j++) {

        // Figure out what sector this group starts in.
        used = group_superblock_overhead(j);

        // Find next array slot in this block (flush block if full).
        slot = j % (TT.blocksize/sizeof(struct ext2_group));
        if (!slot) {
          if (j) xwrite(TT.fsfd, bg, TT.blocksize);
          memset(bg, 0, TT.blocksize);
        }

        // How many free inodes in this group?
        temp = TT.inodespg;
        if (!i) temp -= INODES_RESERVED;
        if (temp > treeinodes) {
          treeinodes -= temp;
          temp = 0;
        } else {
          temp -= treeinodes;
          treeinodes = 0;
        }
        bg[slot].free_inodes_count = SWAP_LE16(temp);

        // How many free blocks in this group?
        temp = TT.inodespg/(TT.blocksize/sizeof(struct ext2_inode)) + 2;
        temp = end-used-temp;
        if (temp > treeblocks) {
          treeblocks -= temp;
          temp = 0;
        } else {
          temp -= treeblocks;
          treeblocks = 0;
        }
        bg[slot].free_blocks_count = SWAP_LE32(temp);

        // Fill out rest of group structure
        used += j*TT.blockbits;
        bg[slot].block_bitmap = SWAP_LE32(used++);
        bg[slot].inode_bitmap = SWAP_LE32(used++);
        bg[slot].inode_table = SWAP_LE32(used);
        bg[slot].used_dirs_count = 0;  // (TODO)
      }
      xwrite(TT.fsfd, bg, TT.blocksize);
    }

    // Now write out stuff that every block group has.

    // Write block usage bitmap

    start += 2 + itable;
    memset(toybuf, 0, TT.blocksize);
    bits_set(toybuf, 0, start);
    bits_set(toybuf, end, TT.blockbits-end);
    temp = TT.treeblocks - usedblocks;
    if (temp) {
      if (end-start > temp) temp = end-start;
      bits_set(toybuf, start, temp);
    }
    xwrite(TT.fsfd, toybuf, TT.blocksize);

    // Write inode bitmap
    memset(toybuf, 0, TT.blocksize);
    j = 0;
    if (!i) bits_set(toybuf, 0, j = INODES_RESERVED);
    bits_set(toybuf, TT.inodespg, slot = TT.blockbits-TT.inodespg);
    temp = TT.treeinodes - usedinodes;
    if (temp) {
      if (slot-j > temp) temp = slot-j;
      bits_set(toybuf, j, temp);
    }
    xwrite(TT.fsfd, toybuf, TT.blocksize);

    // Write inode table for this group (TODO)
    for (j = 0; j<TT.inodespg; j++) {
      slot = j % (TT.blocksize/sizeof(struct ext2_inode));
      if (!slot) {
        if (j) xwrite(TT.fsfd, in, TT.blocksize);
        memset(in, 0, TT.blocksize);
      }
      if (!i && j<INODES_RESERVED) {
        // Write root inode
        if (j == 2) fill_inode(in+slot, dtb);
      } else if (dti) {
        fill_inode(in+slot, dti);
        dti = treenext(dti);
      }
    }
    xwrite(TT.fsfd, in, TT.blocksize);

    while (dtb) {
      // TODO write index data block
      // TODO write root directory data block
      // TODO write directory data block
      // TODO write file data block
      put_zeroes(TT.blocksize);
      start++;
      if (start == end) break;
    }
    // Write data blocks (TODO)
    put_zeroes((end-start) * TT.blocksize);
  }
}
