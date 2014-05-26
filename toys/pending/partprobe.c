/* partprobe.c - Tell the kernel about partition table changes
 *
 * Copyright 2014 Bertold Van den Bergh <vandenbergh@bertold.org>
 *
 * see http://man7.org/linux/man-pages/man8/partprobe.8.html

USE_PARTPROBE(NEWTOY(partprobe, NULL, TOYFLAG_SBIN))

config PARTPROBE
  bool "partprobe"
  default n
  help
    partprobe - Tell the kernel about partition table changes
	
    usage: partprobe [devices...]

    Asks the kernel to re-read the partition table on the specified
    devices.
*/

#include "toys.h"

void update_device(char* path)
{
  int sd_fd = open(path, 0);
  
  if(sd_fd < 0){
	perror_msg("Unable to open %s", path);
	return;
  }
  
  if(ioctl(sd_fd, BLKRRPART, NULL) < 0)
    perror_msg("ioctl (BLKRRPART) failed, old layout still used");
  
  close(sd_fd);
}

void partprobe_main(void)
{
  char** opt; 
  if(*toys.optargs == NULL){
    printf("No devices specified.\n");
    exit(EXIT_FAILURE);
  }
  
  for (opt = toys.optargs; *opt; opt++) {
    update_device(*opt);
  }
  
  exit(EXIT_SUCCESS);
}
