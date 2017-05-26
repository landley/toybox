/* klogd.c - Klogd, The kernel log Dameon.
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No standard

USE_KLOGD(NEWTOY(klogd, "c#<1>8n", TOYFLAG_SBIN))

config KLOGD
    bool "klogd"
    default n
    help
    usage: klogd [-n] [-c N]

    -c  N   Print to console messages more urgent than prio N (1-8)"
    -n    Run in foreground

config KLOGD_SOURCE_RING_BUFFER
    bool "enable kernel ring buffer as log source."
    default n
    depends on KLOGD
*/

#define FOR_klogd
#include "toys.h"
#include <signal.h>
#include <sys/klog.h>
GLOBALS(
  long level;

  int fd;
)

static void set_log_level(int level)
{   
  if (CFG_KLOGD_SOURCE_RING_BUFFER)
    klogctl(8, NULL, level);
  else {
    FILE *fptr = xfopen("/proc/sys/kernel/printk", "w");
    fprintf(fptr, "%u\n", level);
    fclose(fptr);
    fptr = NULL;
  }
}

static void handle_signal(int sig)
{
  if (CFG_KLOGD_SOURCE_RING_BUFFER) {
    klogctl(7, NULL, 0); 
    klogctl(0, NULL, 0);
  } else {
    set_log_level(7);
    xclose(TT.fd);
  }
  syslog(LOG_NOTICE,"KLOGD: Daemon exiting......");
  exit(1);
}

/*
 * Read kernel ring buffer in local buff and keep track of
 * "used" amount to track next read to start.
 */
void klogd_main(void)
{
  int prio, size, used = 0;
  char *start, *line_start, msg_buffer[16348]; //LOG_LINE_LENGTH - Ring buffer size

  sigatexit(handle_signal);
  if (toys.optflags & FLAG_c) set_log_level(TT.level);    //set log level
  if (!(toys.optflags & FLAG_n)) daemon(0, 0);            //Make it daemon

  if (CFG_KLOGD_SOURCE_RING_BUFFER) {
    syslog(LOG_NOTICE, "KLOGD: started with Kernel ring buffer as log source\n");
    klogctl(1, NULL, 0);
  } else {
    TT.fd = xopenro("/proc/kmsg"); //_PATH_KLOG in paths.h
    syslog(LOG_NOTICE, "KLOGD: started with /proc/kmsg as log source\n");
  }
  openlog("Kernel", 0, LOG_KERN);    //open connection to system logger..

  while(1) {
    start = msg_buffer + used; //start updated for re-read.
    if (CFG_KLOGD_SOURCE_RING_BUFFER) {
      size = klogctl(2, start, sizeof(msg_buffer) - used - 1);
    } else {
      size = xread(TT.fd, start, sizeof(msg_buffer) - used - 1);
    }
    if (size < 0) perror_exit("error reading file:");
    start[size] = '\0';  //Ensure last line to be NUL terminated.
    if (used) start = msg_buffer;
    while(start) {
      if ((line_start = strsep(&start, "\n")) != NULL && start != NULL) used = 0;
      else {                            //Incomplete line, copy it to start of buff.
        used = strlen(line_start);
        strcpy(msg_buffer, line_start);
        if (used < (sizeof(msg_buffer) - 1)) break;
        used = 0; //we have buffer full, log it as it is.
      }
      prio = LOG_INFO;  //we dont know priority, mark it INFO
      if (*line_start == '<') {  //we have new line to syslog
        line_start++;
        if (line_start) prio = (int)strtoul(line_start, &line_start, 10);
        if (*line_start == '>') line_start++;
      }
      if (*line_start) syslog(prio, "%s", line_start);
    }
  }
}
