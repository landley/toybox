/* blkid.c - Prints type, label and UUID of filesystem(s).
 *
 * Copyright 2013 Brad Conroy <bconroy@uis.edu>
 *
 * See ftp://ftp.kernel.org/pub/linux/utils/util-linux/v2.24/libblkid-docs/api-index-full.html

USE_BLKID(NEWTOY(blkid, 0, TOYFLAG_BIN))
USE_FSTYPE(NEWTOY(fstype, "<1", TOYFLAG_BIN))

config BLKID
  bool "blkid"
  default y
  help
    usage: blkid DEV...

    Prints type, label and UUID of filesystem on a block device or image.

config FSTYPE
  bool "fstype"
  default y
  help
    usage: fstype DEV...

    Prints type of filesystem on a block device or image.
*/

#define FOR_blkid
#include "toys.h"

struct fstype {
  char *name;
  uint64_t magic;
  int magic_len, magic_offset, uuid_off, label_len, label_off;
};

static const struct fstype fstypes[] = {
  {"ext2", 0xEF53, 2, 1080, 1128, 16, 1144}, // keep this first for ext3/4 check
  {"swap", 0x4341505350415753LL, 8, 4086, 1036, 15, 1052},
  // NTFS label actually 8/16 0x4d80 but horrible: 16 bit wide characters via
  // codepage, something called a uuid that's only 8 bytes long...
  {"ntfs", 0x5346544e, 4, 3, 0x48+(8<<24), 0, 0},

  {"adfs", 0xadf5, 2, 0xc00, 0,0,0},
  {"bfs", 0x1badface, 4, 0, 0,0,0},
  {"btrfs", 0x4D5F53665248425FULL, 8, 65600, 65803, 256, 65819},
  {"cramfs", 0x28cd3d45, 4, 0, 0, 16, 48},
  {"f2fs", 0xF2F52010, 4, 1024, 1132, 16, 1110},
  {"jfs", 0x3153464a, 4, 32768, 32920, 16, 32904},
  {"nilfs", 0x3434, 2, 1030, 1176, 80, 1192},
  {"reiserfs", 0x724573496552ULL, 6, 8244, 8276, 16, 8292},
  {"reiserfs", 0x724573496552ULL, 6, 65588, 65620, 16, 65636},
  {"romfs", 0x2d6d6f72, 4, 0, 0,0,0},
  {"squashfs", 0x73717368, 4, 0, 0,0,0},
  {"xiafs", 0x012fd16d, 4, 572, 0,0,0},
  {"xfs", 0x42534658, 4, 0, 32, 12, 108},
  {"vfat", 0x3233544146ULL, 5, 82, 67+(4<<24), 11, 71},  // fat32
  {"vfat", 0x31544146, 4, 54, 39+(4<<24), 11, 43}     // fat1
};

static void do_blkid(int fd, char *name)
{
  int off, i, j, len;
  char *type;

  off = i = 0;

  for (;;) {
    int pass = 0;

    // Read next block of data
    len = readall(fd, toybuf, sizeof(toybuf));
    if (len != sizeof(toybuf)) return;

    // Iterate through types in range
    for (i=0; i < sizeof(fstypes)/sizeof(struct fstype); i++) {
      uint64_t test;

      // Skip tests not in this 4k block
      if (fstypes[i].magic_offset > off+sizeof(toybuf)) {
        pass++;
        continue;
      }
      if (fstypes[i].magic_offset < off) continue;

      // Populate 64 bit little endian magic value
      test = 0;
      for (j = 0; j < fstypes[i].magic_len; j++)
        test += ((uint64_t)toybuf[j+fstypes[i].magic_offset-off])<<(8*j);
      if (test == fstypes[i].magic) break;
    }

    if (i == ARRAY_LEN(fstypes)) {
      off += len;
      if (pass) continue;
      return;
    }
    break;
  }

  // distinguish ext2/3/4
  type = fstypes[i].name;
  if (!i) {
    if (toybuf[1116]&4) type = "ext3";
    if (toybuf[1120]&64) type = "ext4";
  }

  // Could special case NTFS here...

  // Output for fstype
  if (*toys.which->name == 'f') {
    puts(type);
    return;
  }

  // output for blkid
  printf("%s:",name);

  if (fstypes[i].label_len) {
    char *s = toybuf+fstypes[i].label_off-off;

    len = fstypes[i].label_len;
    if (!strcmp(type, "vfat")) {
      while (len && s[len-1]==' ') len--;
      if (strstart(&s, "NO NAME")) len=0;
    }
    if (len && *s) printf(" LABEL=\"%.*s\"", len, s);
  }

  if (fstypes[i].uuid_off) {
    int bits = 0x550, size = fstypes[i].uuid_off >> 24,
        uoff = (fstypes[i].uuid_off & ((1<<24)-1))-off;

    if (size) bits = 4*(size == 4);
    else size = 16;

    printf(" UUID=\"");
    for (j = 0; j < size; j++) printf("-%02x"+!(bits & (1<<j)), toybuf[uoff+j]);
    printf("\"");
  }

  printf(" TYPE=\"%s\"\n", type);
}

void blkid_main(void)
{
  if (*toys.optargs) loopfiles(toys.optargs, do_blkid);
  else {
    unsigned int ma, mi, sz, fd;
    char *name = toybuf, *buffer = toybuf+1024, device[32];
    FILE *fp = xfopen("/proc/partitions", "r");

    while (fgets(buffer, 1024, fp)) {
      *name = 0;
      if (sscanf(buffer, " %u %u %u %[^\n ]", &ma, &mi, &sz, name) != 4)
        continue;

      sprintf(device, "/dev/%.20s", name);
      if (-1 == (fd = open(device, O_RDONLY))) {
        if (errno != ENOMEDIUM) perror_msg_raw(device);
      } else {
        do_blkid(fd, device);
        close(fd);
      }
    }
    if (CFG_TOYBOX_FREE) fclose(fp);
  }
}

void fstype_main(void)
{
  loopfiles(toys.optargs, do_blkid);
}
