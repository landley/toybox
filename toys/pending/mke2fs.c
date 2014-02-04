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

  struct ext2_superblock sb;
)

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
        if (current->st.st_ino == that->st.st_ino &&
          current->st.st_dev == that->st.st_dev)
        {
          current->st.st_nlink++;
          current->st.st_ino = inode;
        }
      }
    }
    current->st.st_ino = inode;
    current = treenext(current);
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
  uint32_t usedblocks, usedinodes, dtiblk, dtbblk;
  struct dirtree *dti, *dtb;

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

  init_superblock(&TT.sb);

  // Start writing.  Skip the first 1k to avoid the boot sector (if any).
  put_zeroes(1024);

  // Loop through block groups, write out each one.
  dtiblk = dtbblk = usedblocks = usedinodes = 0;
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
