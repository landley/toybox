/* watchdog - start a watchdog timer with configurable kick frequencies

 Author: Chris Sarra, chrissarra@google.com
 Date: 25 July 2019
 Ref: kernel.org/doc/Documentation/watchdog/watchdog-api.txt

USE_WATCHDOG(NEWTOY(watchdog, "Ft#T#", TOYFLAG_NEEDROOT|TOYFLAG_BIN))

config WATCHDOG
  bool "watchdog"
  default y
  help
    usage: watchdog [-F] [-t SW_TIMER_S] [-T HW_TIMER_S] DEV

    Start the watchdog timer at DEV with optional timeout parameters.
    -F run in the foreground (do not daemonize)
    -t software timer (in seconds)
    -T hardware timer (in seconds)
*/
#define FOR_watchdog
#include "toys.h"
#include "linux/watchdog.h"

// Make sure no DEBUG variable is set; change this if you need debug prints!
#undef WATCHDOG_DEBUG

// Default timeout values in seconds.
#define WATCHDOG_SW_TIMER_S_DEFAULT (4)
#define WATCHDOG_HW_TIMER_S_DEFAULT (60)

GLOBALS(
  long hw_timer_s;
  long sw_timer_s;
  int fd;
)

static int intercept_signals(void (*fn)(int)) {
  int rc = 0;
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_handler = fn;

  rc = sigaction(SIGTERM, &sigact, NULL);
#if defined(WATCHDOG_DEBUG)
  if ( rc ) {
    printf("failed to create new sigaction for SIGTERM: %d\n", rc);
  }
#endif
  return rc;
}

void safe_shutdown(int __attribute__((unused))ignored) {
  write(TT.fd, "V", 1);
  close(TT.fd);
  TT.fd = -1;
  error_exit("safely exited watchdog.");
}

void watchdog_main(void) {
  int rc = 0;
  long hw_timer_sec = 0;
  char *watchdog_dev = NULL;

  if ( toys.optc > 0 ) {
    watchdog_dev = toys.optargs[0];
#if defined(WATCHDOG_DEBUG)
    printf("using dev @ '%s'\n", watchdog_dev);
#endif
  } else {
    error_exit("watchdog dev required");
  }

  // Set default values for timeouts if no flags
  if (!(toys.optflags & FLAG_t)) {
    TT.sw_timer_s = WATCHDOG_SW_TIMER_S_DEFAULT;
#if defined(WATCHDOG_DEBUG)
    printf("using default sw_timer_s.\n");
#endif
  }

  if (!(toys.optflags & FLAG_T)) {
    TT.hw_timer_s = WATCHDOG_HW_TIMER_S_DEFAULT;
#if defined(WATCHDOG_DEBUG)
    printf("using default hw_timer_s.\n");
#endif
  }

#if defined(WATCHDOG_DEBUG)
    printf("hw timer: %ld seconds\n", TT.hw_timer_s);
    printf("sw timer: %ld seconds\n", TT.sw_timer_s);
#endif

  if (!(toys.optflags & FLAG_F)) {
#if defined(WATCHDOG_DEBUG)
      printf("daemonizing. so long, foreground!\n");
#endif
    // Attempt to daemonize
    rc = daemon(1, 1);
    if ( rc ) {
      perror_exit("failed to daemonize: %d", rc);
    }
  }

  // Intercept terminating signals so we can call our shutdown routine first.
  if ( intercept_signals(safe_shutdown) ) {
    perror_exit("failed to intercept desired signals: %d", rc);
  }
#if defined(WATCHDOG_DEBUG)
    printf("Successfully intercepted signals.\n");
#endif

  TT.fd = open(watchdog_dev, O_WRONLY);
  if ( TT.fd == -1 ) {
    perror_exit("failed to open '%s'", watchdog_dev);
  }

#if defined(WDIOC_SETTIMEOUT)
  // SETTIMEOUT takes time value in seconds: s = ms / (1000 ms/s)
  hw_timer_sec = TT.hw_timer_s;
  xioctl(TT.fd, WDIOC_SETTIMEOUT, (void *)&hw_timer_sec);
#endif // WDIOC_SETTIMEOUT

  // Now that we've got the watchdog device open, kick it periodically.
  while (1) {
    write(TT.fd, "\0", 1);
    usleep(TT.sw_timer_s * 1000 * 1000);
  }
}
