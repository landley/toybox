/* klogd.c - Klogd, The kernel log Dameon.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *
 * No standard

USE_KLOGD(NEWTOY(klogd, "c#<1>8n", TOYFLAG_SBIN))

config KLOGD
    bool "klogd"
    default y
    help
    usage: klogd [-n] [-c N]

    -c  N   Print to console messages more urgent than prio N (1-8)"
    -n    Run in foreground.

config KLOGD_SOURCE_RING_BUFFER
    bool "enable kernel ring buffer as log source."
    default n
    depends on KLOGD
*/

#define FOR_klogd
#include "toys.h"

GLOBALS(
  long level;
  int fd;
)

#if CFG_KLOGD_SOURCE_RING_BUFFER    
#include <sys/klog.h>
/*
 * Open klogd with ring buffer as log source
 */
static void open_klogd(void)  
{
  syslog(LOG_NOTICE, "KLOGD: started with Kernel ring buffer as log source\n");
  klogctl(1, NULL, 0);
}
/*
 * Read kernel ring buffer
 */
static int read_klogd(char *bufptr, int len)
{
  return klogctl(2, bufptr, len);
}
/*
 * Set log level to LEVEL
 */
static void set_log_level(int level)
{   
  klogctl(8, NULL, level);
}
/*
 * Close klog
 */
static void close_klogd(void)
{
  klogctl(7, NULL, 0); 
  klogctl(0, NULL, 0);
}
#else
#include<paths.h>
#ifndef _PATH_KLOG
#error "_PATH_KLOG is not known"
#endif
/*
 * Open klog with /proc/kmsg as log source
 */
static void open_klogd(void)
{
  TT.fd = xopen(_PATH_KLOG, O_RDONLY);
  syslog(LOG_NOTICE, "KLOGD: started with /proc/kmsg as log source\n");
}
/*
 * Read log to local buffer
 */
static int read_klogd(char *bufptr, int len)
{
  return xread(TT.fd, bufptr, len);
}
/*
 * Set log level to LEVEL by writing to PATH_PRINTK
 */
static void set_log_level(int level)
{
    FILE *fptr = xfopen("/proc/sys/kernel/printk", "w");
    fprintf(fptr, "%u\n", level);
    fclose(fptr);
    fptr = NULL;
}
/* 
 * set log level while exiting
 */
static void close_klogd(void)
{
  set_log_level(7);
  xclose(TT.fd);
}
#endif
/*
 * Handle signals
 */
static void handle_signal(int sig)
{
  close_klogd();
  syslog(LOG_NOTICE,"KLOGD: Daemon exiting......");
  exit(1);
}

static int go_daemon(void)
{                
  int fd;        

  fd = open("/dev/null", O_RDWR);
  if (fd < 0) fd = open("/", O_RDONLY, 0666);
  pid_t pid = fork();

  if (pid < 0) { 
    error_msg("DAEMON: fail to fork");
    return -1;   
  }              
  if (pid) exit(EXIT_SUCCESS);

  setsid();      
  dup2(fd, 0);   
  dup2(fd, 1);   
  dup2(fd, 2);   
  if (fd > 2) close(fd);   
  return 0;      
}

/*
 * Read kernel ring buffer in local buff and keep track of
 * "used" amount to track next read to start.
 */
void klogd_main(void)
{
  char msg_buffer[16348]; //LOG_LINE_LENGTH - Ring buffer size
  int prio, size;
  int used = 0;
  char *start, *line_start;

  sigatexit(handle_signal);
  if(toys.optflags & FLAG_c) set_log_level(TT.level);    //set log level
  if(!(toys.optflags & FLAG_n)) go_daemon();        //Make it daemon
  open_klogd();    
  openlog("Kernel", 0, LOG_KERN);    //open connection to system logger..

  while(1) {
    start = msg_buffer + used; //start updated for re-read.
    size = read_klogd(start, sizeof(msg_buffer)-1-used);
    if (size < 0) perror_exit("error reading file:");
    start[size] = '\0';  //Ensure last line to be NUL terminated.
    if(used) start = msg_buffer;
    while(1) {
      if((line_start = strsep(&start, "\n")) != NULL && start != NULL) used = 0;
      else {                            //Incomplete line, copy it to start of buff.
        used = strlen(line_start);
        strcpy(msg_buffer, line_start);
        if(used < (sizeof(msg_buffer) - 1)) break;
        used = 0; //we have buffer full, log it as it is.
      }
      prio = LOG_INFO;  //we dont know priority, mark it INFO
      if(*line_start == '<') {  //we have new line to syslog
        line_start++;
        if(line_start) prio = (int)strtoul(line_start, &line_start, 10);
        if(*line_start == '>') line_start++;
      }
      if(*line_start) syslog(prio, "%s", line_start);
    }
  }
}  
