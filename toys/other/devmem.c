/* devmem.c - Access physical addresses
 *
 * Copyright 2019 The Android Open Source Project

USE_DEVMEM(NEWTOY(devmem, "<1>3", TOYFLAG_USR|TOYFLAG_SBIN))

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

unsigned long xatolu(char *str, int bytes)
{
  char *end = str;
  unsigned long lu;

  errno = 0;
  lu = strtoul(str, &end, 0);
  // Report out of range values as errors rather than truncating.
  if (errno == ERANGE || lu > (~0UL)>>(sizeof(long)-bytes)*8)
    error_exit("%s>%d bytes", str, bytes);
  if (*end || errno) perror_exit("bad %s", str);

  return lu;
}

void devmem_main(void)
{
  int writing = toys.optc == 3, page_size = sysconf(_SC_PAGESIZE), bytes = 4,fd;
  unsigned long data = 0, map_off, map_len,
    addr = xatolu(*toys.optargs, sizeof(long));
  char *sizes = sizeof(long)==8 ? "1248" : "124";
  void *map, *p;

  // WIDTH?
  if (toys.optc>1) {
    int i;

    if ((i=stridx(sizes, *toys.optargs[1]))==-1 || toys.optargs[1][1])
      error_exit("bad width: %s", toys.optargs[1]);
    bytes = 1<<i;
  }

  // DATA?
  if (writing) data = xatolu(toys.optargs[2], bytes);

  // Map in just enough.
  if (CFG_TOYBOX_FORK) {
    fd = xopen("/dev/mem", (writing ? O_RDWR : O_RDONLY) | O_SYNC);

    map_off = addr & ~(page_size - 1ULL);
    map_len = (addr+bytes-map_off);
    map = xmmap(0, map_len, writing ? PROT_WRITE : PROT_READ, MAP_SHARED, fd,
        map_off);
    p = map + (addr & (page_size - 1));
    close(fd);
  } else p = (void *)addr;

  // Not using peek()/poke() because registers care about size of read/write
  if (writing) {
    if (bytes==1) *(char *)p = data;
    else if (bytes==2) *(unsigned short *)p = data;
    else if (bytes==4) *(unsigned int *)p = data;
    else if (sizeof(long)==8 && bytes==8) *(unsigned long *)p = data;
  } else {
    if (bytes==1) data = *(char *)p;
    else if (bytes==2) data = *(unsigned short *)p;
    else if (bytes==4) data = *(unsigned int *)p;
    else if (sizeof(long)==8 && bytes==8) data = *(unsigned long *)p;
    printf((!strchr(*toys.optargs, 'x')) ? "%0*ld\n" : "0x%0*lx\n",
      bytes*2, data);
  }

  if (CFG_TOYBOX_FORK) munmap(map, map_len);
}
