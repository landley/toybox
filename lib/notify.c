#include "toys.h"

static char **paths;
static int *fds;
static int count;

#ifdef __APPLE__

#include <sys/event.h>

static int kq = -1;

void notify_init(int max)
{
  if ((kq = kqueue()) == -1) perror_exit("kqueue");
  paths = xmalloc(max * sizeof(char *));
  fds = xmalloc(max * sizeof(int));
}

int notify_add(int fd, char *path)
{
  struct kevent event;

  EV_SET(&event, fd, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, NULL);
  if (kevent(kq, &event, 1, NULL, 0, NULL) == -1 || event.flags & EV_ERROR)
    return -1;
  paths[count] = path;
  fds[count++] = fd;
  return 0;
}

int notify_wait(char **path)
{
  struct kevent event;
  int i;

  for (;;) {
    if (kevent(kq, NULL, 0, &event, 1, NULL) != -1) {
      // We get the fd for free, but still have to search for the path.
      for (i=0; i<count; i++) if (fds[i]==event.ident) {
        *path = paths[i];
        return event.ident;
      }
    }
  }
}

#else

#include <sys/inotify.h>

static int ffd = -1;
static int *ids;

void notify_init(int max)
{
  if ((ffd = inotify_init()) < 0) perror_exit("inotify_init");
  fds = xmalloc(max * sizeof(int));
  ids = xmalloc(max * sizeof(int));
  paths = xmalloc(max * sizeof(char *));
}

int notify_add(int fd, char *path)
{
  ids[count] = inotify_add_watch(ffd, path, IN_MODIFY);
  if (ids[count] == -1) return -1;
  paths[count] = path;
  fds[count++] = fd;
  return 0;
}

int notify_wait(char **path)
{
  struct inotify_event ev;
  int i;

  for (;;) {
    if (sizeof(ev)!=read(ffd, &ev, sizeof(ev))) perror_exit("inotify");

    for (i=0; i<count; i++) if (ev.wd==ids[i]) {
      *path = paths[i];
      return fds[i];
    }
  }
}

#endif
