/* mix.c - A very basic mixer.
 *
 * Copyright 2014 Brad Conroy, dedicated to the Public Domain.
 *

USE_MIX(NEWTOY(mix, "m:d:l#r#", TOYFLAG_USR|TOYFLAG_BIN))
config MIX
  bool "mix"
  default n
  help
   usage: mix [-m mixer] [-d device] [-l level / left level] [-r right level]

   Lists/sets mixer devices/levels.
*/

#define FOR_mix
#include <linux/soundcard.h>
#include "toys.h"


GLOBALS(
   int right;
   int level;
   char *device;
   char *mixer;
)

void mix_main(void)
{
  const char *devices[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
  char *mixer_name=(toys.optflags & FLAG_m)?TT.mixer:"/dev/mixer";
  int i, mask, device=-1, level,
      mixer=xopen(mixer_name, O_RDWR|O_NONBLOCK);

  xioctl(mixer, SOUND_MIXER_READ_DEVMASK,&mask);

  if (!(toys.optflags & FLAG_d)){
    for (i = 0; i < SOUND_MIXER_NRDEVICES; ++i)
      if (1<<i & mask) printf("%s\n",devices[i]);
    return;
  }else{
    for (i = 0; i < SOUND_MIXER_NRDEVICES; ++i){
      if ((1<<i & mask) && !strcmp(devices[i], TT.device)){
        device=i;
        break;
      }
    }
    if (-1==device) return; //with error
  }

  if (!(toys.optflags & FLAG_l)){
    xioctl(mixer, MIXER_READ(device),&level);
    if (0xFF < level) printf("%s:%s = left:%d\t right:%d\n", mixer_name,
                             devices[device], level>>8, level & 0xFF);
    else printf("%s:%s = %d\n",mixer_name, devices[device], level);
    return;
  }

  level=TT.level;
  if (!(toys.optflags & FLAG_r)) level = TT.right | (level<<8);

  xioctl(mixer, MIXER_WRITE(device),&level);
  close(mixer);
}
