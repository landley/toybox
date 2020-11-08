/* devmem.c - Access physical addresses
 *
 * Copyright 2019 The Android Open Source Project

USE_DEVMEM(NEWTOY(devmem, "<1>3", TOYFLAG_USR|TOYFLAG_BIN))

config DEVMEM
  bool "devmem"
  default y
  help
    usage: devmem ADDR [WIDTH [DATA]]

    Read/write physical address. WIDTH is 1, 2, 4, or 8 bytes (default 4).
    Prefix ADDR with 0x for hexadecimal, output is in same base as address.
*/

#define FOR_devmem
#include "toys.h"

void devmem_main(void)
{
  int writing = toys.optc == 3, page_size = sysconf(_SC_PAGESIZE), bytes = 4,fd;
  unsigned long long data = 0, map_off, map_len;
  unsigned long addr = atolx(*toys.optargs);
  void *map, *p;

  // WIDTH?
  if (toys.optc>1) {
    int i;

    if ((i=stridx("1248", *toys.optargs[1]))==-1 || toys.optargs[1][1])
      error_exit("bad width: %s", toys.optargs[1]);
    bytes = 1<<i;
  }

  // DATA? Report out of range values as errors rather than truncating.
  if (writing) data = atolx_range(toys.optargs[2], 0, (1ULL<<(8*bytes))-1);

  // Map in just enough.
  if (CFG_TOYBOX_FORK) {
    fd = xopen("/dev/mem", (writing ? O_RDWR : O_RDONLY) | O_SYNC);

    map_off = addr & ~(page_size - 1);
    map_len = (addr+bytes-map_off);
    map = xmmap(0, map_len, writing ? PROT_WRITE : PROT_READ, MAP_SHARED, fd,
        map_off);
    p = map + (addr & (page_size - 1));
    close(fd);
  } else p = (void *)addr;

  // Not using peek()/poke() because registers care about size of read/write
  if (writing) {
    if (bytes == 1) *(unsigned char *)p = data;
    else if (bytes == 2) *(unsigned short *)p = data;
    else if (bytes == 4) *(unsigned int *)p = data;
    else if (bytes == 8) *(unsigned long long *)p = data;
  } else {
    if (bytes == 1) data = *(unsigned char *)p;
    else if (bytes == 2) data = *(unsigned short *)p;
    else if (bytes == 4) data = *(unsigned int *)p;
    else if (bytes == 8) data = *(unsigned long long *)p;
    printf((!strchr(*toys.optargs, 'x')) ? "%0*lld\n" : "0x%0*llx\n",
      bytes*2, data);
  }

  if (CFG_TOYBOX_FORK) munmap(map, map_len);
}
