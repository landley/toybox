/* blkdiscard - discard device sectors
 *
 * Copyright 2020 Patrick Oppenlander <patrick.oppenlander@gmail.com>
 *
 * See http://man7.org/linux/man-pages/man8/blkdiscard.8.html
 *
 * The -v and -p options are not supported.
 * Size parsing does not match util-linux where MB, GB, TB are multiples of
 * 1000 and MiB, TiB, GiB are multipes of 1024.

USE_BLKDISCARD(NEWTOY(blkdiscard, "<1>1f(force)l(length)#<0o(offset)#<0s(secure)z(zeroout)[!sz]", TOYFLAG_BIN))

config BLKDISCARD
  bool "blkdiscard"
  default y
  help
    usage: blkdiscard [-olszf] DEVICE

    Discard device sectors.

    -o, --offset OFF	Byte offset to start discarding at (default 0)
    -l, --length LEN	Bytes to discard (default all)
    -s, --secure		Perform secure discard
    -z, --zeroout		Zero-fill rather than discard
    -f, --force		Disable check for mounted filesystem

    OFF and LEN must be aligned to the device sector size.
    By default entire device is discarded.
    WARNING: All discarded data is permanently lost!
*/

#define FOR_blkdiscard
#include "toys.h"

#include <linux/fs.h>

GLOBALS(
  long o, l;
)

void blkdiscard_main(void)
{
  int fd = xopen(*toys.optargs, O_WRONLY|O_EXCL*!FLAG(f));
  unsigned long long ol[2];

  // TODO: if numeric arg was long long array could live in TT.
  ol[0] = TT.o;
  if (FLAG(l)) ol[1] = TT.l;
  else {
    xioctl(fd, BLKGETSIZE64, ol+1);
    ol[1] -= ol[0];
  }
  xioctl(fd, FLAG(s) ? BLKSECDISCARD : FLAG(z) ? BLKZEROOUT : BLKDISCARD, ol);
  close(fd);
}
