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

#include <cutils/properties.h>

static const char *services[] = {
  "netd", "surfaceflinger", "zygote", "zygote_secondary", NULL,
};

static void start_stop(int start)
{
  const char* property = start ? "ctl.start" : "ctl.stop";
  int i;

  if (getuid() != 0) error_exit("must be root");

  if (*toys.optargs) {
    for (i = 0; toys.optargs[i]; i++) property_set(property, toys.optargs[i]);
  } else if (start) {
    for (i = 0; i < ARRAY_LEN(services); ++i) {
      property_set(property, services[i]);
    }
  } else {
    for (i = ARRAY_LEN(services) - 1; i >= 0; --i) {
      property_set(property, services[i]);
    }
  }
}

void start_main(void)
{
  start_stop(1);
}

#define CLEANUP_start
#define FOR_stop
#include "generated/flags.h"

void stop_main(void)
{
  start_stop(0);
}
