/* eject.c - eject device.
 *
 * Copyright 2012 Harvind Singh <harvindsingh1981@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gamil.com>
 * Not in SUSv4.

USE_EJECT(NEWTOY(eject, ">1stT[!tT]", TOYFLAG_USR|TOYFLAG_BIN))

config EJECT
  bool "eject"
  default y
  help
    usage: eject [-s] [-t] [-T] [...]

    Eject DEVICE or default /dev/cdrom

    -s  SCSI device
    -t  Close tray
    -T  Open/close tray (toggle).
*/

#define FOR_eject
#include "toys.h"
#include <scsi/sg.h>
#include <scsi/scsi.h>

#define CDROM_DRIVE_STATUS 0x5326 //Get tray position, etc.
#define CDROM_CLOSE_TRAY 0x5319   //Pendant of CDROM_EJECT.
#define CDROM_EJECT 0x5309        //Ejects the cdrom media.
#define DRIVE_NOT_READY 3         //Drive is busy.

/*
 * Remove SCSI Device.
 */
static void remove_scsi(int dscptr)
{
  char sg_driver_cmd[3][6] = {
    { ALLOW_MEDIUM_REMOVAL, 0, 0, 0, 0, 0 }, //allow medium removal
    { START_STOP, 0, 0, 0, 1, 0 }, //start the motor
    { START_STOP, 0, 0, 0, 2, 0 } //eject the media
  };

  unsigned i;
  unsigned char buffer1[32],buffer2[2];
  sg_io_hdr_t in_out_header;

  if ((ioctl(dscptr, SG_GET_VERSION_NUM, &i) < 0) || (i < 30000))
    error_exit("not a sg device or old sg driver");

  memset(&in_out_header, 0, sizeof(sg_io_hdr_t));
  in_out_header.interface_id = 'S';
  in_out_header.cmd_len = 6;
  in_out_header.mx_sb_len = sizeof(buffer1);
  in_out_header.dxfer_direction = SG_DXFER_NONE;
  in_out_header.dxferp = buffer2;
  in_out_header.sbp = buffer1;
  in_out_header.timeout = 2000;

  for (i = 0; i < 3; i++) {
    in_out_header.cmdp = (void *)sg_driver_cmd[i];
    xioctl(dscptr, SG_IO, (void *)&in_out_header);
  }
  /* force kernel to reread partition table when new disc is inserted */
  ioctl(dscptr, BLKRRPART);
}

/*
 * eject main function.
 */
void eject_main(void)
{
  int fd, out = 0, rc = 0;
  char *device_name;
  if (!toys.optc) device_name = "/dev/cdrom";
  else device_name = toys.optargs[0];

  fd = xopen(device_name, O_RDONLY | O_NONBLOCK);
  if (!toys.optflags) xioctl(fd, CDROM_EJECT, &out);
  else if (toys.optflags & FLAG_s) remove_scsi(fd);
  else {
    if (toys.optflags & FLAG_T || toys.optflags & FLAG_t) {
      rc = ioctl(fd, CDROM_DRIVE_STATUS, &out);
      if (toys.optflags & FLAG_t || rc == 2) //CDS_TRAY_OPEN = 2
        xioctl(fd, CDROM_CLOSE_TRAY, &out);
      else xioctl(fd, CDROM_EJECT, &out);
    }
  }
  xclose(fd);
}
