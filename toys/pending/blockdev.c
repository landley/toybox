/* blockdev.c -show/set blockdev information.
 *
 * Copyright 2014 Sameer Prakash Pradhan <sameer.p.pradhan@gmail.com>
 *
 * No Standard.
 *

USE_BLOCKDEV(NEWTOY(blockdev, "<1>1(setro)(setrw)(getro)(getss)(getbsz)(setbsz)#<0(getsz)(getsize)(getsize64)(flushbufs)(rereadpt)",TOYFLAG_USR|TOYFLAG_BIN))

config BLOCKDEV
  bool "blockdev"
  default n
  help
  usage:blockdev OPTION BLOCKDEV
 
 
  setro	Set ro
  setrw	Set rw
  getro	Get ro
  getss	Get sector size
  getbsz Get block size
  setbsz BYTES	Set block size
  getsz Get device size in 512-byte sectors
  getsize Get device size in sectors (deprecated)
  getsize64	Get device size in bytes
  flushbufs	Flush buffers
  rereadpt Reread partition table
*/

#define FOR_blockdev
#include "toys.h"
#include <linux/fs.h>

GLOBALS(
  long bsz;
)

void blockdev_main(void)
{
  long long val = 0;
  int cmd, fd, set = 0;

  switch (toys.optflags) {
    case FLAG_setro:
      cmd = BLKROSET;
      val = set = 1;
      break;
    case FLAG_setrw:
      cmd = BLKROSET;
      set = 1;
      break;
    case FLAG_getro:
      cmd = BLKROGET;           
      break;
    case FLAG_getss:
      cmd = BLKSSZGET;           
      break;
    case FLAG_getbsz:
      cmd = BLKBSZGET;            
      break;
    case FLAG_setbsz:
      cmd = BLKBSZSET;
      set = 1;
      val = TT.bsz;            
      break;
    case FLAG_getsz:
      cmd = BLKGETSIZE64;            
      break;
    case FLAG_getsize:
      cmd = BLKGETSIZE;            
      break;
    case FLAG_getsize64:
      cmd = BLKGETSIZE64;            
      break;
    case FLAG_flushbufs:
      cmd = BLKFLSBUF;
      set = 1;
      break;
    case FLAG_rereadpt:
      cmd = BLKRRPART;
      set = 1;
      break;
    default:
      toys.exithelp = 1;
      error_exit(NULL);
  }
  fd = xopen(*toys.optargs, O_RDONLY);
  xioctl(fd, cmd, &val);
  if (!set) 
    printf("%lld\n",  ((toys.optflags & FLAG_getsz)?val >> 9: val));          
  if (CFG_TOYBOX_FREE) xclose(fd);
}
