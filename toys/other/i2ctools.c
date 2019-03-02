/* i2ctools.c - i2c tools
 *
 * Copyright 2018 The Android Open Source Project
 *
 * https://www.kernel.org/doc/Documentation/i2c/
 *
 * Note: -y must have the same value in each toy for `confirm`.
 *
 * TODO: i2cdetect -q/-r and the "auto" mode?
 * TODO: i2cdump non-byte modes, -r FIRST-LAST?
 * TODO: i2cget non-byte modes? default to current read address?
 * TODO: i2cset -r? -m MASK? c/s modes, p mode modifier?

USE_I2CDETECT(NEWTOY(i2cdetect, ">3aFly", TOYFLAG_USR|TOYFLAG_BIN))
USE_I2CDUMP(NEWTOY(i2cdump, "<2>2fy", TOYFLAG_USR|TOYFLAG_BIN))
USE_I2CGET(NEWTOY(i2cget, "<3>3fy", TOYFLAG_USR|TOYFLAG_BIN))
USE_I2CSET(NEWTOY(i2cset, "<4fy", TOYFLAG_USR|TOYFLAG_BIN))

config I2CDETECT
  bool "i2cdetect"
  default y
  help
    usage: i2cdetect [-ary] BUS [FIRST LAST]
    usage: i2cdetect -F BUS
    usage: i2cdetect -l

    Detect i2c devices.

    -a	All addresses (0x00-0x7f rather than 0x03-0x77)
    -F	Show functionality
    -l	List all buses
    -r	Probe with SMBus Read Byte
    -y	Answer "yes" to confirmation prompts (for script use)

config I2CDUMP
  bool "i2cdump"
  default y
  help
    usage: i2cdump [-fy] BUS CHIP

    Dump i2c registers.

    -f	Force access to busy devices
    -y	Answer "yes" to confirmation prompts (for script use)

config I2CGET
  bool "i2cget"
  default y
  help
    usage: i2cget [-fy] BUS CHIP ADDR

    Read an i2c register.

    -f	Force access to busy devices
    -y	Answer "yes" to confirmation prompts (for script use)

config I2CSET
  bool "i2cset"
  default y
  help
    usage: i2cset [-fy] BUS CHIP ADDR VALUE... MODE

    Write an i2c register. MODE is b for byte, w for 16-bit word, i for I2C block.

    -f	Force access to busy devices
    -y	Answer "yes" to confirmation prompts (for script use)
*/

#define FOR_i2cdetect
#include "toys.h"

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

printf_format static void confirm(const char *fmt, ...)
{
  va_list va;

  if (toys.optflags & FLAG_y) return;

  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
  if (!yesno(1)) error_exit("Exiting");
}

static int i2c_open(int bus, int slave, int chip)
{
  int fd;

  snprintf(toybuf, sizeof(toybuf), "/dev/i2c-%d", bus);
  fd = xopen(toybuf, O_RDONLY);
  if (slave) xioctl(fd, slave, (void *)(long)chip);
  return fd;
}

static unsigned long i2c_get_funcs(int bus)
{
  int fd = i2c_open(bus, 0, 0);
  unsigned long result;

  xioctl(fd, I2C_FUNCS, &result);
  close(fd);
  return result;
}

static int i2c_read_byte(int fd, int addr, int *byte)
{
  struct i2c_smbus_ioctl_data ioctl_data;
  union i2c_smbus_data data;

  memset(&data, 0, sizeof(data));
  ioctl_data.read_write = I2C_SMBUS_READ;
  ioctl_data.size = I2C_SMBUS_BYTE_DATA;
  ioctl_data.command = addr;
  ioctl_data.data = &data;
  if (ioctl(fd, I2C_SMBUS, &ioctl_data) == -1) return -1;
  *byte = data.byte;
  return 0;
}

static void i2cdetect_dash_F(int bus)
{
  size_t i;

  struct { int mask; const char *name; } funcs[] = {
    {I2C_FUNC_I2C, "I2C"},
    {I2C_FUNC_SMBUS_QUICK, "SMBus Quick Command"},
    {I2C_FUNC_SMBUS_WRITE_BYTE, "SMBus Send Byte"},
    {I2C_FUNC_SMBUS_READ_BYTE, "SMBus Receive Byte"},
    {I2C_FUNC_SMBUS_WRITE_BYTE_DATA, "SMBus Write Byte"},
    {I2C_FUNC_SMBUS_READ_BYTE_DATA, "SMBus Read Byte"},
    {I2C_FUNC_SMBUS_WRITE_WORD_DATA, "SMBus Write Word"},
    {I2C_FUNC_SMBUS_READ_WORD_DATA, "SMBus Read Word"},
    {I2C_FUNC_SMBUS_PROC_CALL, "SMBus Process Call"},
    {I2C_FUNC_SMBUS_WRITE_BLOCK_DATA, "SMBus Write Block"},
    {I2C_FUNC_SMBUS_READ_BLOCK_DATA, "SMBus Read Block"},
    {I2C_FUNC_SMBUS_BLOCK_PROC_CALL, "SMBus Block Process Call"},
    {I2C_FUNC_SMBUS_PEC, "SMBus PEC"},
    {I2C_FUNC_SMBUS_WRITE_I2C_BLOCK, "I2C Write Block"},
    {I2C_FUNC_SMBUS_READ_I2C_BLOCK, "I2C Read Block"},
  };
  unsigned long supported = i2c_get_funcs(bus);

  printf("Functionalities implemented by %s:\n", toybuf);
  for (i = 0; i < ARRAY_LEN(funcs); ++i) {
    printf("%-32s %s\n", funcs[i].name,
           (supported & funcs[i].mask) ? "yes" : "no");
  }
}

static int i2cdetect_dash_l(struct dirtree *node)
{
  int suffix_len = strlen("/name");
  int bus;
  char *fname, *p;
  unsigned long funcs;

  if (!node->parent) return DIRTREE_RECURSE; // Skip the directory itself.

  if (sscanf(node->name, "i2c-%d", &bus) != 1) return 0;
  funcs = i2c_get_funcs(bus);

  fname = dirtree_path(node, &suffix_len);
  strcat(fname, "/name");
  xreadfile(fname, toybuf, sizeof(toybuf));
  free(fname);
  if ((p = strchr(toybuf, '\n'))) *p = 0;

  // "i2c-1	i2c       	Synopsys DesignWare I2C adapter 	I2C adapter"
  printf("%s\t%-10s\t%-32s\t%s\n", node->name,
         (funcs & I2C_FUNC_I2C) ? "i2c" : "?",
         toybuf,
         (funcs & I2C_FUNC_I2C) ? "I2C Adapter" : "?");

  return 0;
}

void i2cdetect_main(void)
{
  if (toys.optflags & FLAG_l) {
    if (toys.optc) error_exit("-l doesn't take arguments");
    dirtree_read("/sys/class/i2c-dev", i2cdetect_dash_l);
  } else if (toys.optflags & FLAG_F) {
    if (toys.optc != 1) error_exit("-F BUS");
    i2cdetect_dash_F(atolx_range(*toys.optargs, 0, INT_MAX));
  } else {
    int bus, first = 0x03, last = 0x77, fd, row, addr, byte;

    if (toys.optflags & FLAG_a) {
      first = 0x00;
      last = 0x7f;
    }

    if (toys.optc != 1 && toys.optc != 3) error_exit("bad args");
    bus = atolx_range(*toys.optargs, 0, INT_MAX);
    if (toys.optc == 3) {
      first = atolx_range(toys.optargs[1], 0, 0x7f);
      last = atolx_range(toys.optargs[1], 0, 0x7f);
      if (first > last) error_exit("first > last");
    }

    confirm("Probe chips 0x%02x-0x%02x on bus %d?", first, last, bus);

    fd = i2c_open(bus, 0, 0);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (row = 0; row <= 0x70; row += 16) {
      xprintf("%02x:", row & 0xf0);
      for (addr = row; addr < row + 16; ++addr) {
        if (addr < first || addr > last) printf("   ");
        else {
          if (ioctl(fd, I2C_SLAVE, addr) == -1) {
            if (errno == EBUSY) {
              xprintf(" UU");
              continue;
            }
            perror_exit("ioctl(I2C_SLAVE)");
          }
          if (i2c_read_byte(fd, addr, &byte) == -1) xprintf(" --");
          else xprintf(" %02x", addr);
        }
      }
      putchar('\n');
    }
    close(fd);
  }
}

#define CLEANUP_i2cdetect
#define FOR_i2cdump
#include "generated/flags.h"

void i2cdump_main(void)
{
  int bus = atolx_range(toys.optargs[0], 0, INT_MAX);
  int chip = atolx_range(toys.optargs[1], 0, 0x7f);
  int fd, row, addr, byte;

  confirm("Dump chip 0x%02x on bus %d?", chip, bus);

  fd = i2c_open(bus, (toys.optflags&FLAG_f)?I2C_SLAVE_FORCE:I2C_SLAVE, chip);
  printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef\n");
  for (row = 0; row <= 0xf0; row += 16) {
    xprintf("%02x:", row & 0xf0);
    for (addr = row; addr < row + 16; ++addr) {
      if (i2c_read_byte(fd, addr, &byte) == -1) perror_exit("i2c_read_byte");
      printf(" %02x", byte);
      toybuf[addr-row] = isprint(byte) ? byte : (byte ? '?' : '.');
    }
    printf("    %16.16s\n", toybuf);
  }
  close(fd);
}

#define CLEANUP_i2cdump
#define FOR_i2cget
#include "generated/flags.h"

void i2cget_main(void)
{
  int bus = atolx_range(toys.optargs[0], 0, INT_MAX);
  int chip = atolx_range(toys.optargs[1], 0, 0x7f);
  int addr = atolx_range(toys.optargs[2], 0, 0xff);
  int fd, byte;

  confirm("Read register 0x%02x from chip 0x%02x on bus %d?", addr, chip, bus);

  fd = i2c_open(bus, (toys.optflags&FLAG_f)?I2C_SLAVE_FORCE:I2C_SLAVE, chip);
  if (i2c_read_byte(fd, addr, &byte) == -1) perror_exit("i2c_read_byte");
  printf("0x%02x\n", byte);
  close(fd);
}

#define CLEANUP_i2cget
#define FOR_i2cset
#include "generated/flags.h"

void i2cset_main(void)
{
  int bus = atolx_range(toys.optargs[0], 0, INT_MAX);
  int chip = atolx_range(toys.optargs[1], 0, 0x7f);
  int addr = atolx_range(toys.optargs[2], 0, 0xff);
  char *mode = toys.optargs[toys.optc-1];
  int fd, i;
  struct i2c_smbus_ioctl_data ioctl_data;
  union i2c_smbus_data data;

  memset(&data, 0, sizeof(data));
  if (strlen(mode) != 1) help_exit("mode too long");
  if (*mode == 'b' && toys.optc == 5) {
    ioctl_data.size = I2C_SMBUS_BYTE_DATA;
    data.byte = atolx_range(toys.optargs[3], 0, 0xff);
  } else if (*mode == 'w' && toys.optc == 5) {
    ioctl_data.size = I2C_SMBUS_WORD_DATA;
    data.word = atolx_range(toys.optargs[3], 0, 0xffff);
  } else if (*mode == 'i' && toys.optc >= 5) {
    if (toys.optc - 4 > I2C_SMBUS_BLOCK_MAX) error_exit("too much data");
    ioctl_data.size = I2C_SMBUS_I2C_BLOCK_DATA;
    for (i = 0; i < toys.optc - 4; ++i)
      data.block[i+1] = atolx_range(toys.optargs[3+i], 0, 0xff);
    data.block[0] = toys.optc - 4;
  } else {
    help_exit("syntax error");
  }

  confirm("Write register 0x%02x from chip 0x%02x on bus %d?", addr, chip, bus);

  fd = i2c_open(bus, (toys.optflags&FLAG_f)?I2C_SLAVE_FORCE:I2C_SLAVE, chip);
  ioctl_data.read_write = I2C_SMBUS_WRITE;
  ioctl_data.command = addr;
  ioctl_data.data = &data;
  xioctl(fd, I2C_SMBUS, &ioctl_data);
  close(fd);
}
