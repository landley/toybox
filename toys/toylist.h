/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */


struct df_data {
	struct arg_list *fstype;

	long units;
};

struct dmesg_data {
	long level;
	long size;
};

struct mke2fs_data {
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
};

struct mkfifo_data {
	char *mode;
};

struct netcat_data {
	char *filename;        // -f read from filename instead of network
	long quit_delay;       // -q Exit after EOF from stdin after # seconds.
	char *source_address;  // -s Bind to a specific source address.
	long port;             // -p Bind to a specific source port.
	long listen;           // -l Listen for connection instead of dialing out.
	long wait;             // -w Wait # seconds for a connection.
	long delay;            // -i delay between lines sent
};

struct oneit_data {
	char *console;
};

struct patch_data {
	char *infile;
	long prefix;

	struct double_list *plines, *flines;
	long oldline, oldlen, newline, newlen, linenum;
	int context, state, filein, fileout, filepatch;
	char *tempname, *oldname;
};

struct sed_data {
	struct arg_list *commands;
};

struct sleep_data {
	long seconds;
};

struct touch_data {
	char *ref_file;
	char *time;
	long length;
};

struct toysh_data {
	char *command;
};

extern union toy_union {
	struct dmesg_data dmesg;
	struct df_data df;
	struct mke2fs_data mke2fs;
	struct mkfifo_data mkfifo;
	struct netcat_data netcat;
	struct oneit_data oneit;
	struct patch_data patch;
	struct sed_data sed;
	struct sleep_data sleep;
	struct touch_data touch;
	struct toysh_data toysh;
} toy;

#define TOYFLAG_USR      (1<<0)
#define TOYFLAG_BIN      (1<<1)
#define TOYFLAG_SBIN     (1<<2)
#define TOYMASK_LOCATION ((1<<4)-1)

#define TOYFLAG_NOFORK   (1<<4)

extern struct toy_list {
	char *name;
	void (*toy_main)(void);
	char *options;
	int flags;
} toy_list[];
