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
