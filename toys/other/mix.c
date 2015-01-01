/* mix.c - A very basic mixer.
 *
 * Copyright 2014 Brad Conroy, dedicated to the Public Domain.
 *

USE_MIX(NEWTOY(mix, "c:d:l#r#", TOYFLAG_USR|TOYFLAG_BIN))

config MIX
  bool "mix"
  default y
  help
   usage: mix [-d DEV] [-c CHANNEL] [-l VOL] [-r RIGHT]

   List OSS sound channels (module snd-mixer-oss), or set volume(s).

   -c CHANNEL	Set/show volume of CHANNEL (default first channel found)
   -d DEV		Device node (default /dev/mixer)
   -l VOL		Volume level
   -r RIGHT	Volume of right stereo channel (with -r, -l sets left volume)
*/

#define FOR_mix
#include "toys.h"
#include <linux/soundcard.h>

GLOBALS(
   long right;
   long level;
   char *dev;
   char *chan;
)

void mix_main(void)
{
  const char *channels[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
  int mask, channel = -1, level, fd;

  if (!TT.dev) TT.dev = "/dev/mixer";
  fd = xopen(TT.dev, O_RDWR|O_NONBLOCK);
  xioctl(fd, SOUND_MIXER_READ_DEVMASK, &mask);

  for (channel = 0; channel < SOUND_MIXER_NRDEVICES; channel++) {
    if ((1<<channel) & mask) {
      if (TT.chan) {
        if (!strcmp(channels[channel], TT.chan)) break;
      } else if (toys.optflags & FLAG_l) break;
      else printf("%s\n", channels[channel]);
    }
  }

  if (!(toys.optflags & (FLAG_c|FLAG_l))) return;
  else if (channel == SOUND_MIXER_NRDEVICES) error_exit("bad -c '%s'", TT.chan);

  if (!(toys.optflags & FLAG_l)) {
    xioctl(fd, MIXER_READ(channel), &level);
    if (level > 0xFF)
      xprintf("%s:%s = left:%d\t right:%d\n",
              TT.dev, channels[channel], level>>8, level & 0xFF);
    else xprintf("%s:%s = %d\n", TT.dev, channels[channel], level);
  } else {
    level = TT.level;
    if (!(toys.optflags & FLAG_r)) level = TT.right | (level<<8);

    xioctl(fd, MIXER_WRITE(channel), &level);
  }

  if (CFG_TOYBOX_FREE) close(fd);
}
