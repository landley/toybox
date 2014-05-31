/* inotifyd.c - inotify daemon. 
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini1981@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_INOTIFYD(NEWTOY(inotifyd, "<2", TOYFLAG_USR|TOYFLAG_BIN))

config INOTIFYD
  bool "inotifyd"
  default n
  help
    usage: inotifyd PROG FILE[:mask] ...

    Run PROG on filesystem changes.
    When a filesystem event matching MASK occurs on FILEn,
    PROG ACTUAL_EVENTS FILEn [SUBFILE] is run.
    If PROG is -, events are sent to stdout.

    Events:
      a   File is accessed
      c   File is modified
      e   Metadata changed
      w   Writable file is closed
      0   Unwritable file is closed
      r   File is opened
      D   File is deleted
      M   File is moved
      u   Backing fs is unmounted
      o   Event queue overflowed
      x   File can't be watched anymore
    If watching a directory:
      m   Subfile is moved into dir
      y   Subfile is moved out of dir
      n   Subfile is created
      d   Subfile is deleted

    inotifyd waits for PROG to exit.
    When x event happens for all FILEs, inotifyd exits.
*/
#define FOR_inotifyd
#include "toys.h"
#include <sys/inotify.h>

GLOBALS(
  int gotsignal;
)

static void sig_handler(int sig)
{
  TT.gotsignal = sig;
}

static int exec_wait(char **args)
{
  int status = 0;
  pid_t pid = xfork();

  if (!pid) xexec(args);
  else waitpid(pid, &status, 0);
  return WEXITSTATUS(status);
}

void inotifyd_main(void)
{
  struct pollfd fds;
  char *prog_args[5], **files, **restore;
  struct mask_nameval {
    char name;
    unsigned long val;
  } mask_nv[] = {
    { 'a', IN_ACCESS },
    { 'c', IN_MODIFY },
    { 'e', IN_ATTRIB },
    { 'w', IN_CLOSE_WRITE },
    { '0', IN_CLOSE_NOWRITE },
    { 'r', IN_OPEN },
    { 'm', IN_MOVED_FROM },
    { 'y', IN_MOVED_TO },
    { 'n', IN_CREATE },
    { 'd', IN_DELETE },
    { 'D', IN_DELETE_SELF },
    { 'M', IN_MOVE_SELF },
    { 'u', IN_UNMOUNT },
    { 'o', IN_Q_OVERFLOW },
    { 'x', IN_IGNORED }
  };
  int mask, masks_len = ARRAY_LEN(mask_nv);

  prog_args[0] = toys.optargs[0];
  prog_args[4] = NULL;
  toys.optc--; // 1st one is program, rest are files to be watched
  //wd ZERO is not used, hence toys.optargs is assigned to files.
  restore = files = toys.optargs;
  if ((fds.fd = inotify_init()) == -1) perror_exit("initialization failed");

  while (*++toys.optargs) {
    char *path = *toys.optargs;
    char *masks = strchr(path, ':');

    mask = 0x0fff; //assuming all non-kernel events to be notified.
    if (masks) {
      *masks++ = '\0';
      mask = 0; 
      while (*masks) {
        int i = 0;

        for (i = 0; i < masks_len; i++) {
          if (*masks == mask_nv[i].name) {
            mask |= mask_nv[i].val;
            break;
          }
        }
        if (i == masks_len) error_exit("wrong mask '%c'", *masks);
        masks++;
      }
    }
    if (inotify_add_watch(fds.fd, path, mask) < 0) 
      perror_exit("add watch '%s' failed", path);
  }
  toys.optargs = restore;
  sigatexit(sig_handler);
  fds.events = POLLIN;

  while (1) {
    int ret = 0, queue_len;
    void *buf = NULL;
    struct inotify_event *event;

retry:
    if (TT.gotsignal) break;
    ret = poll(&fds, 1, -1);
    if (ret < 0 && errno == EINTR) goto retry;
    if (ret <= 0) break;
    xioctl(fds.fd, FIONREAD, &queue_len);
    event = buf = xmalloc(queue_len);
    queue_len = readall(fds.fd, buf, queue_len);
    while (queue_len > 0) {
      uint32_t m = event->mask;

      if (m) {
        char evts[masks_len+1], *s = evts;
        int i;

        for (i = 0; i < masks_len; i++) 
          if (m & mask_nv[i].val) *s++ = mask_nv[i].name;
        *s = '\0';

        if (prog_args[0][0] == '-' && !prog_args[0][1]) { //stdout
          printf(event->len ? "%s\t%s\t%s\n" : "%s\t%s\n", evts,
              files[event->wd], event->name);
          fflush(stdout);
        } else {
          prog_args[1] = evts;
          prog_args[2] = files[event->wd];
          prog_args[3] = event->len ? event->name : NULL;
          exec_wait(prog_args); //exec and wait...
        }
        if (event->mask & IN_IGNORED) {
          if (--toys.optc <= 0) {
            free(buf);
            goto done;
          }
          inotify_rm_watch(fds.fd, event->wd);
        }
      }
      queue_len -= sizeof(struct inotify_event) + event->len;
      event = (void*)((char*)event + sizeof(struct inotify_event) + event->len); //next event
    }
    free(buf);
  }
done:
  toys.exitval = TT.gotsignal;
}
