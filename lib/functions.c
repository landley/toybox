/* vi: set ts=4 :*/
/* functions.c - reusable stuff.
 *
 * Functions with the x prefix never return failure, they either succeed or
 * kill the program with an error message.
 */

#include "toys.h"

// Die with an error message.
void error_exit(char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fprintf(stderr, "%s: ", toys.which->name);
	vfprintf(stderr, msg, args);
	va_end(args);
	exit(toys.exitval);
}

void strlcpy(char *dest, char *src, size_t size)
{
	strncpy(dest,src,size);
	dest[size-1] = 0;
}

// Die unless we can allocate memory.
void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret) error_exit("xmalloc");
}

// Die unless we can copy this string.
void *xstrndup(char *s, size_t n)
{
	void *ret = xmalloc(++n);
	strlcpy(ret, s, n);
	
	return ret;
}

// Die unless we can exec argv[]
void *xexec(char **argv)
{
	execvp(argv[0], argv);
	error_exit("No %s", argv[0]);
}

// Die unless we can open/create a file, returning file descriptor.
int xopen(char *path, int flags, int mode)
{
	int fd = open(path, flags, mode);
	if (fd == -1) error_exit("No file %s\n", path);
	return fd;
}

// Die unless we can open/create a file, returning FILE *.
FILE *xfopen(char *path, char *mode)
{
	FILE *f = fopen(path, mode);
	if (!f) error_exit("No file %s\n", path);
	return f;
}
