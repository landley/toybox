/* blkid.c - Prints type, label and UUID of filesystem(s).
 *
 * Copyright 2013 Brad Conroy <bconroy@uis.edu>
 *
 * See ftp://ftp.kernel.org/pub/linux/utils/util-linux/v2.24/libblkid-docs/api-index-full.html

USE_BLKID(NEWTOY(blkid, NULL, TOYFLAG_BIN))

config BLKID
  bool "blkid"
  default n
  help
    usage: blkid [block device...]
    Prints type, label and UUID of filesystem.

*/
#define FOR_blkid
#include "toys.h"

#define BIT(x) (1<<(x))
#define _MATCH(buf,type,offset,match) (((unsigned type *)&buf[offset])[0]==(match))
#define MATCH2(b,a) (_MATCH(b,a##_TYPE,a##_OFFSET,a##_MAGIC2)||MATCH(b,a))
#define MATCH3(b,a) (_MATCH(b,a##_TYPE,a##_OFFSET,a##_MAGIC3)||MATCH2(b,a))
#define MATCH(b,a) _MATCH(b,a##_TYPE,a##_OFFSET,a##_MAGIC)
/* may need to check endianess for C2I */
#define C2I(a,b,c,d) (a|(b<<8)|(c<<16)|(d<<24))

typedef struct fs_info{
  unsigned uuid_off;
  unsigned lab_off;
  short uuid_t;
  short lab_len;
}fs_info_t;

//#define SET_FS(t) (fs_info_t){t##_UUID,t##_LABEL,t##_UUID_T,t##_LABEL_SZ}
#define SET_FS(t) (fs_info_t)t##_INFO

#define ADFS_MAGIC			0xadf5
#define ADFS_TYPE			short
#define ADFS_OFFSET			0xc00
#define ADFS_INFO			{0,0,0,0}	/*todo*/

#define BFS_MAGIC			0x1badface
#define BFS_TYPE			long
#define BFS_OFFSET			0
#define BFS_INFO			{0,0,0,0}

/*won't fit in 1 toybuf*/
#define BTRFS_MAGIC			C2I('_','B','H','R')	//'RHB_'
//#define BTRFS_MAGIC2		'M_Sf'	/*not currently used*/
//#define BTRFS_OFFSET2		65604	/*not currently used*/
#define BTRFS_TYPE			long	/*change to long long if full magic used?*/
#define BTRFS_OFFSET		65600
#define BTRFS_INFO			{65803,65819,16,256}

#define CRAMFS_MAGIC		0x28cd3d45
#define CRAMFS_MAGIC2		0x453dcd28
#define CRAMFS_TYPE			long
#define CRAMFS_OFFSET		0
#define CRAMFS_INFO			{0,48,0,16}

#define EXT_MAGIC			0xEF53
#define EXT_MAGIC2			0xEF51
#define EXT_MAGIC3			0x53EF
#define EXT_TYPE			short
#define EXT_OFFSET			1080
#define EXT3_BYTE			1116
#define EXT4_BYTE			1120
#define EXT_INFO			{1128,1144,16,16}

#define F2FS_MAGIC			0xF2F52010
#define F2FS_TYPE			long
#define F2FS_OFFSET			1024
#define F2FS_INFO			{1132,1110,16,16}

/*won't fit in 1 toybuf*/
#define JFS_MAGIC			C2I('J','S','F','1')	//'1SFJ'
#define JFS_TYPE			long
#define JFS_OFFSET			32768
#define JFS_INFO			{32920,32904,16,16}

#define NILFS_MAGIC			0x3434
#define NILFS_TYPE			short
#define NILFS_OFFSET		1030
#define NILFS_INFO			{1176,1192,16,80}

#define NTFS_MAGIC			C2I('N','T','F','S')	//'SFTN'
#define NTFS_TYPE			long
#define NTFS_OFFSET			3
#define NTFS_INFO			{0x48,0X4D80,8,-8}

/*won't fit in 1 toybuf*/
#define REISER_MAGIC		C2I('R','e','I','s')	//'sIeR'
#define REISER_TYPE			long
#define REISER_OFFSET		8244
#define REISER_INFO			{8276,8292,16,16}

#define REISERB_MAGIC		REISER_MAGIC	//'sIeR'
#define REISERB_TYPE		REISER_TYPE
#define REISERB_OFFSET		65588
#define REISERB_INFO		{65620,65636,16,16}

#define ROMFS_MAGIC			C2I('r','o','m','-') //'-mor'
#define ROMFS_OFFSET		0
#define ROMFS_TYPE			long
#define ROMFS_INFO			{0,0,0,0}

#define SQUASHFS_OFFSET		0
#define SQUASHFS_TYPE		long
#define SQUASHFS_MAGIC		0x73717368
#define SQUASHFS_INFO		{0,0,0,0}

#define XIAFS_OFFSET		572
#define XIAFS_TYPE			long
#define XIAFS_MAGIC			0x012FD16D
#define XIAFS_INFO			{0,0,0,0}

#define XFS_OFFSET			0
#define XFS_TYPE			long
#define XFS_MAGIC			C2I('X','F','S','B')
#define XFS_INFO			{32,108,16,12}

#if (CFG_BLKID)
char *toutf8(unsigned short * ws){	/* hack for NTFS utf16 */
  static char buf[16];
  int i=0;
  while((buf[i++]=*ws++));
  return buf;
}
#endif

/* TODO if no args use proc/partitions */
void do_blkid(int fd, char *name){	
  fs_info_t fs;
  char *fstype;
  unsigned char buf[66560];  /*toybuf is only 4096, could rework logic*/
  int sb_read;

  sb_read = read(fd, &buf, sizeof(buf));
  if (sb_read < 1) return;

  if MATCH(buf,ADFS){
    fstype="adfs";
    if (CFG_BLKID) fs=SET_FS(ADFS);
  }else if MATCH(buf,BFS){
    fstype="bfs";
    if (CFG_BLKID) fs=SET_FS(BFS);
  }else if MATCH(buf,CRAMFS){
    fstype="cramfs";
    if (CFG_BLKID) fs=SET_FS(CRAMFS);
  }else if MATCH3(buf,EXT){
    fstype=(buf[EXT3_BYTE]&BIT(2))?((buf[EXT4_BYTE]&BIT(6))?"ext4":"ext3"):"ext2";
    if (CFG_BLKID) fs=SET_FS(EXT);
  }else if MATCH(buf,F2FS){
    fstype="f2fs";
    if (CFG_BLKID) fs=SET_FS(F2FS);
  }else if MATCH(buf,NILFS){
    fstype="nilfs";
    if (CFG_BLKID) fs=SET_FS(NILFS);
  }else if MATCH(buf,NTFS){
    fstype="ntfs";
    if (CFG_BLKID) fs=SET_FS(NTFS);
  }else if MATCH(buf,XIAFS){
    fstype="xiafs";
    if (CFG_BLKID) fs=SET_FS(XIAFS);
/*below here would require more than 1 toybuf*/
  }else if MATCH(buf,REISER){
    fstype="reiserfs";
    if (CFG_BLKID) fs=SET_FS(REISER);
  }else if MATCH(buf,JFS){
    fstype="jfs";
    if (CFG_BLKID) fs=SET_FS(JFS);
  }else if MATCH(buf,BTRFS){
    fstype="btrfs";
    if (CFG_BLKID) fs=SET_FS(BTRFS);
  }else if MATCH(buf,REISERB){
    fstype="reiserfs";
    if (CFG_BLKID) fs=SET_FS(REISERB);
  }else return;

if (CFG_BLKID  && !strncmp("blkid",toys.which->name,5) ){
  printf("%s:",name);
  if ( fs.lab_len > 0 )
    printf(" LABEL=\"%.*s\"", fs.lab_len, (char *)&buf[fs.lab_off]);
  else if ( fs.lab_len < 0 )
    printf(" LABEL=\"%.*s\"", -fs.lab_len,
      toutf8((unsigned short *)&buf[fs.lab_off]));
  if ( fs.uuid_off > 0 ){
    if ( fs.uuid_t == 16 )
      printf(" UUID=\"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
      "%02x%02x%02x%02x%02x%02x\"",
      buf[fs.uuid_off], buf[fs.uuid_off+1],
      buf[fs.uuid_off+2], buf[fs.uuid_off+3],
      buf[fs.uuid_off+4], buf[fs.uuid_off+5],
      buf[fs.uuid_off+6], buf[fs.uuid_off+7],
      buf[fs.uuid_off+8], buf[fs.uuid_off+9],
      buf[fs.uuid_off+10], buf[fs.uuid_off+11],
      buf[fs.uuid_off+12], buf[fs.uuid_off+13],
      buf[fs.uuid_off+14], buf[fs.uuid_off+15]);
    if ( fs.uuid_t == 8 )
      printf(" UUID=\"%02X%02X%02X%02X%02X%02X%02X%02X\"",
      buf[fs.uuid_off+7], buf[fs.uuid_off+6],
      buf[fs.uuid_off+5], buf[fs.uuid_off+4],
      buf[fs.uuid_off+3], buf[fs.uuid_off+2],
      buf[fs.uuid_off+1], buf[fs.uuid_off]);
  }
  printf(" TYPE=\"%s\"",fstype);
}else /* fstype */
  write(1,fstype,strlen(fstype));	/* avoid printf overhead in fstype */
  putchar('\n');
}

#if (CFG_BLKID)
void blkid_main(void)
{
  loopfiles(toys.optargs, do_blkid);
}
#endif


/* fstype.c - Prints type of filesystem(s).
 *
 * Copyright 2013 Brad Conroy <bconroy@uis.edu>

USE_FSTYPE(NEWTOY(fstype, NULL, TOYFLAG_BIN))

config FSTYPE
  bool "fstype"
  default n
  help
    usage: fstype [block device...]
    Prints type of filesystem.
*/
#define FOR_fstype
#include "toys.h"
void do_blkid(int fd, char *name);
void fstype_main(void)
{
  loopfiles(toys.optargs, do_blkid);
}
