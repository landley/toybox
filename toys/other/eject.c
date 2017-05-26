/* eject.c - eject device.
 *
 * Copyright 2012 Harvind Singh <harvindsingh1981@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gamil.com>
 *
 * No standard.

USE_EJECT(NEWTOY(eject, ">1stT[!tT]", TOYFLAG_USR|TOYFLAG_BIN))

config EJECT
  bool "eject"
  default y
  help
    usage: eject [-stT] [DEVICE]

    Eject DEVICE or default /dev/cdrom

    -s	SCSI device
    -t	Close tray
    -T	Open/close tray (toggle)
*/

#define FOR_eject
#include "toys.h"
#include <scsi/sg.h>
#include <scsi/scsi.h>

// The SCSI way of requesting eject
static void remove_scsi(int fd)
{
  unsigned i;
  sg_io_hdr_t *header = (sg_io_hdr_t *)(toybuf+64);
  char sg_driver_cmd[3][6] = {
    { ALLOW_MEDIUM_REMOVAL, 0, 0, 0, 0, 0 },
    { START_STOP, 0, 0, 0, 1, 0 }, //start the motor
    { START_STOP, 0, 0, 0, 2, 0 } //eject the media
  };

  header->interface_id = 'S';
  header->cmd_len = 6;
  header->mx_sb_len = 32;
  header->dxfer_direction = SG_DXFER_NONE;
  header->dxferp = toybuf + 32;
  header->sbp = (void *)toybuf;
  header->timeout = 2000;

  for (i = 0; i < 3; i++) {
    header->cmdp = (void *)sg_driver_cmd[i];
    xioctl(fd, SG_IO, (void *)header);
  }

  // force kernel to reread partition table when new disc is inserted
  ioctl(fd, BLKRRPART);
}

/*
 * eject main function.
 */
void eject_main(void)
{
  int fd, out = 0;
  char *device_name = "/dev/cdrom";

  if (*toys.optargs) device_name = *toys.optargs;

  fd = xopen(device_name, O_RDONLY | O_NONBLOCK);
  if (!toys.optflags) xioctl(fd, 0x5309, &out);		// CDROM_EJECT
  else if (toys.optflags & FLAG_s) remove_scsi(fd);
  else {
    if ((toys.optflags & FLAG_T) || (toys.optflags & FLAG_t)) {
      int rc = ioctl(fd, 0x5326, &out);			// CDROM_DRIVE_STATUS
      if ((toys.optflags & FLAG_t) || rc == 2)		// CDS_TRAY_OPEN
        xioctl(fd, 0x5319, &out);			// CDROM_CLOSE_TRAY
      else xioctl(fd, 0x5309, &out);			// CDROM_EJECT
    }
  }
  if (CFG_TOYBOX_FREE) xclose(fd);
}
