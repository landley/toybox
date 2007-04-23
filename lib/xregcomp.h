/* This is a separate file so libc doesn't always need regex support. */

#include <sys/types.h>
#include <regex.h>

void xregcomp(regex_t *preg, char *rexec, int cflags);
