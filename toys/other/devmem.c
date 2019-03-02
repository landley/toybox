/* devmem.c - Access physical addresses
 *
 * Copyright 2019 The Android Open Source Project

USE_DEVMEM(NEWTOY(devmem, "<1>3", TOYFLAG_USR|TOYFLAG_BIN))

config DEVMEM
  bool "devmem"
  default y
  help
    usage: devmem ADDR [WIDTH [DATA]]

    Read/write physical address via /dev/mem.

    WIDTH is 1, 2, 4, or 8 bytes (default 4).
*/

#define FOR_devmem
#include "toys.h"

void devmem_main(void)
{
  int writing = toys.optc == 3, page_size = getpagesize(), bytes = 4, fd;
  unsigned long long addr = atolx(toys.optargs[0]), data = 0, map_off, map_len;
  void *map, *p;

  // WIDTH?
  if (toys.optc>1) {
    int i;

    if (strlen(toys.optargs[1])!=1 || (i=stridx("1248", *toys.optargs[1]))==-1)
      error_exit("bad width: %s", toys.optargs[1]);
    bytes = 1<<i;
  }

  // DATA? Report out of range values as errors rather than truncating.
  if (writing) data = atolx_range(toys.optargs[2], 0, (1ULL<<(8*bytes))-1);

  // Map in just enough.
  fd = xopen("/dev/mem", (writing ? O_RDWR : O_RDONLY) | O_SYNC);
  map_off = addr & ~(page_size - 1);
  map_len = (addr+bytes-map_off);
  map = xmmap(NULL, map_len, writing ? PROT_WRITE : PROT_READ, MAP_SHARED, fd,
      map_off);
  p = map + (addr & (page_size - 1));
  close(fd);

  // Not using peek()/poke() because registers care about size of read/write
  if (writing) {
    if (bytes == 1) *(char *)p = data;
    else if (bytes == 2) *(short *)p = data;
    else if (bytes == 4) *(int *)p = data;
    else if (bytes == 8) *(long long *)p = data;
  } else {
    if (bytes == 1) data = *(char *)p;
    else if (bytes == 2) data = *(short *)p;
    else if (bytes == 4) data = *(int *)p;
    else if (bytes == 8) data = *(long long *)p;
    printf("%#0*llx\n", bytes*2, data);
  }

  munmap(map, map_len);
}
