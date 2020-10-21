/* watchdog - start a watchdog timer with configurable kick frequencies
 *
 * Copyright 2019 Chris Sarra <chrissarra@google.com>
 *
 * See kernel.org/doc/Documentation/watchdog/watchdog-api.txt

USE_WATCHDOG(NEWTOY(watchdog, "<1>1Ft#=4<1T#=60<1", TOYFLAG_NEEDROOT|TOYFLAG_BIN))

config WATCHDOG
  bool "watchdog"
  default y
  depends on TOYBOX_FORK
  help
    usage: watchdog [-F] [-t UPDATE] [-T DEADLINE] DEV

    Start the watchdog timer at DEV with optional timeout parameters.

    -F	run in the foreground (do not daemonize)
    -t	poke watchdog every UPDATE seconds (default 4)
    -T	reboot if not poked for DEADLINE seconds (default 60)
*/

#define FOR_watchdog
#include "toys.h"
#include "linux/watchdog.h"

GLOBALS(
  long T, t;

  int fd;
)

void safe_shutdown(int ignored) {
  write(TT.fd, "V", 1);
  close(TT.fd);
  error_exit("safely exited watchdog.");
}

void watchdog_main(void)
{
  if (!FLAG(F) && daemon(1, 1)) perror_exit("failed to daemonize");
  xsignal(SIGTERM, safe_shutdown);
  xsignal(SIGINT, safe_shutdown);
  xioctl(TT.fd = xopen(*toys.optargs, O_WRONLY), WDIOC_SETTIMEOUT, &TT.T);

  // Now that we've got the watchdog device open, kick it periodically.
  for (;;) {
    write(TT.fd, "", 1);
    sleep(TT.t);
  }
}
