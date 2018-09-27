/* start.c - Start/stop system services.
 *
 * Copyright 2016 The Android Open Source Project

USE_START(NEWTOY(start, "", TOYFLAG_USR|TOYFLAG_SBIN))
USE_STOP(NEWTOY(stop, "", TOYFLAG_USR|TOYFLAG_SBIN))

config START
  bool "start"
  depends on TOYBOX_ON_ANDROID
  default y
  help
    usage: start [SERVICE...]

    Starts the given system service, or netd/surfaceflinger/zygotes.

config STOP
  bool "stop"
  depends on TOYBOX_ON_ANDROID
  default y
  help
    usage: stop [SERVICE...]

    Stop the given system service, or netd/surfaceflinger/zygotes.
*/

#define FOR_start
#include "toys.h"

static void start_stop(int start)
{
  char *property = start ? "ctl.start" : "ctl.stop";
  // null terminated in both directions
  char *services[] = {0,"netd","surfaceflinger","zygote","zygote_secondary",0},
       **ss = toys.optargs;
  int direction = 1;

  if (getuid()) error_exit("must be root");

  if (!*ss) {
    // If we don't have optargs, iterate through services forward/backward.
    ss = services+1;
    if (!start) ss = services+ARRAY_LEN(services)-2, direction = -1;
  }

  for (; *ss; ss += direction)
    if (__system_property_set(property, *ss))
      error_exit("failed to set property '%s' to '%s'", property, *ss);
}

void start_main(void)
{
  start_stop(1);
}

void stop_main(void)
{
  start_stop(0);
}
