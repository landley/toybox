/* blkdiscard - discard device sectors
 *
 * Copyright 2020 Patrick Oppenlander <patrick.oppenlander@gmail.com>
 *
 * See https://git.kernel.org/pub/scm/utils/util-linux/util-linux.git/tree/sys-utils/blkdiscard.8
 *
 * These options are not supported:
 * -v, --verbose
 * -p, --step
 * -V, --version
 *
 * Size parsing does not match util-linux where MB, GB, TB are multiples of
 * 1000 and MiB, TiB, GiB are multipes of 1024.

USE_BLKDISCARD(NEWTOY(blkdiscard, "<1>1f(force)l(length):o(offset):s(secure)z(zeroout)", TOYFLAG_BIN))

config BLKDISCARD
  bool "blkdiscard"
  default n
  help
    usage: blkdiscard [options] device

    Discard device sectors.

    -o, --offset OFF	Byte offset from which to start discarding
    -l, --length LEN	Number of bytes to discard
    -s, --secure		Perform secure discard
    -z, --zeroout		Zero-fill rather than discard
    -f, --force		Disable checking for mounted filesystem

    By default OFF is zero and LEN is the device size meaning that the entire
    device will be discarded.

    OFF and LEN must be aligned to the device sector size.

    WARNING: All discarded data will be permanently lost!
*/

#define FOR_blkdiscard
#include "toys.h"

#include <linux/fs.h>

GLOBALS(
  char *offset, *length;
)

void blkdiscard_main(void)
{
  int fd, req = BLKDISCARD;
  uint64_t off = 0, len;

  fd = xopen(toys.optargs[0], O_WRONLY | (FLAG(f) ? 0 : O_EXCL));

  if (FLAG(o)) off = atolx_range(TT.offset, 0, LLONG_MAX);

  if (FLAG(l)) len = atolx_range(TT.length, 0, LLONG_MAX);
  else {
    if (ioctl(fd, BLKGETSIZE64, &len) < 0) {
      perror_msg("ioctl %x", (int)BLKGETSIZE64);
      goto out;
    }
    len -= off;
  }

  if (FLAG(s) && FLAG(z)) {
    error_msg("secure and zeroout are mutually exclusive");
    goto out;
  }
  if (FLAG(s)) req = BLKSECDISCARD;
  if (FLAG(z)) req = BLKZEROOUT;

  if (ioctl(fd, req, (uint64_t[]){off, len}) < 0)
    perror_msg("ioctl %x", req);

out:
  if (CFG_TOYBOX_FREE) close(fd);
}
