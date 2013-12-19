/* pending.c - reusable stuff awaiting review
 *
 * new lib entries for stuff in toys/pending
 */

#include "toys.h"

void daemonize(void)
{
  int fd = open("/dev/null", O_RDWR);
  if (fd < 0) fd = xcreate("/", O_RDONLY, 0666);

  pid_t pid = fork();
  if (pid < 0) perror_exit("DAEMON: failed to fork");
  if (pid) exit(EXIT_SUCCESS);

  setsid();
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  if (fd > 2) close(fd);
}

char *human_readable(unsigned long long size)
{
  static char buf[32];
  char *tmp = (buf+4); //unsigned long long  can come in 20byte string.
  int index, sz;

  for (index = 0; 1024 < size>>(10*index); index++);
  sz = size>>(10*index);
  if (sz < 10 && index) {
    sprintf(tmp, "%llu", size>>(10*(index-1)));
    sprintf(buf, "%c.%c", tmp[0], tmp[1]);
  } else sprintf(buf, "%u", sz);
  sprintf(buf, "%s%c", buf, " KMGTPE"[index]);
  return buf;
}
