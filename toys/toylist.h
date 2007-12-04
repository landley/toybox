/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */


// Provide function declarations and structs.  Note that main.c #includes this
// file twice (with different macros) to populate toy_list[].

#ifndef NEWTOY
#define NEWTOY(name, opts, flags) void name##_main(void);
#define OLDTOY(name, oldname, opts, flags)

struct df_data {
	struct arg_list *fstype;

	long units;
};

// Still to go: "E:jJ:L:m:O:"
#define MKE2FS_OPTSTRING "<1>2g:Fnqm#N#i#b#"

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

struct netcat_data {
	char *filename;        // -f read from filename instead of network
	long quit_delay;       // -q Exit after EOF from stdin after # seconds.
	char *source_address;  // -s Bind to a specific source address.
	long port;             // -p Bind to a specific source port.
	long listen;           // -l Listen for connection instead of dialing out.
	long wait;             // -w Wait # seconds for a connection.
	long delay;            // -i delay between lines sent
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

struct mkfifo_data {
	char *mode;
};

extern union toy_union {
	struct dmesg_data dmesg;
	struct df_data df;
	struct mke2fs_data mke2fs;
	struct mkfifo_data mkfifo;
	struct netcat_data netcat;
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

#endif

// List of all the applets toybox can provide.

// This one is out of order on purpose: it's the first element in the array.

NEWTOY(toybox, NULL, 0)

// The rest of these are alphabetical, for binary search.

USE_BASENAME(NEWTOY(basename, "<1>2", TOYFLAG_BIN))
USE_BZCAT(NEWTOY(bzcat, "", TOYFLAG_USR|TOYFLAG_BIN))
USE_CATV(NEWTOY(catv, "vte", TOYFLAG_USR|TOYFLAG_BIN))
USE_CHROOT(NEWTOY(chroot, "<1", TOYFLAG_USR|TOYFLAG_SBIN))
USE_COUNT(NEWTOY(count, "", TOYFLAG_USR|TOYFLAG_BIN))
USE_TOYSH(NEWTOY(cd, NULL, TOYFLAG_NOFORK))
USE_DF(NEWTOY(df, "Pkt*a", TOYFLAG_USR|TOYFLAG_SBIN))
USE_DIRNAME(NEWTOY(dirname, "<1>1", TOYFLAG_BIN))
USE_DMESG(NEWTOY(dmesg, "s#n#c", TOYFLAG_BIN))
USE_ECHO(NEWTOY(echo, "+en", TOYFLAG_BIN))
USE_TOYSH(NEWTOY(exit, NULL, TOYFLAG_NOFORK))
USE_FALSE(NEWTOY(false, NULL, TOYFLAG_BIN))
USE_HELLO(NEWTOY(hello, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_HELP(NEWTOY(help, "<1", TOYFLAG_BIN))
USE_MKE2FS(NEWTOY(mke2fs, MKE2FS_OPTSTRING, TOYFLAG_SBIN))
USE_MKFIFO(NEWTOY(mkfifo, "<1m:", TOYFLAG_BIN))
USE_NETCAT(OLDTOY(nc, netcat, "i#w#l@p#s:q#f:e", TOYFLAG_BIN))
USE_NETCAT(NEWTOY(netcat, "i#w#l@p#s:q#f:e", TOYFLAG_BIN))
USE_ONEIT(NEWTOY(oneit, "+<1p", TOYFLAG_SBIN))
USE_PWD(NEWTOY(pwd, NULL, TOYFLAG_BIN))
USE_READLINK(NEWTOY(readlink, "<1f", TOYFLAG_BIN))
USE_TOYSH(OLDTOY(sh, toysh, "c:i", TOYFLAG_BIN))
USE_SHA1SUM(NEWTOY(sha1sum, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_SLEEP(NEWTOY(sleep, "<1", TOYFLAG_BIN))
USE_SYNC(NEWTOY(sync, NULL, TOYFLAG_BIN))
USE_TOUCH(NEWTOY(touch, "l#t:r:mca", TOYFLAG_BIN))
USE_TOYSH(NEWTOY(toysh, "c:i", TOYFLAG_BIN))
USE_TRUE(NEWTOY(true, NULL, TOYFLAG_BIN))
USE_TTY(NEWTOY(tty, "s", TOYFLAG_BIN))
USE_WHICH(NEWTOY(which, "a", TOYFLAG_USR|TOYFLAG_BIN))
USE_YES(NEWTOY(yes, "", TOYFLAG_USR|TOYFLAG_BIN))
