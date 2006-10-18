/* vi: set sw=4 ts=4 :*/
/* functions.c - reusable stuff.
 *
 * Functions with the x prefix are wrappers for library functions.  They either
 * succeed or kill the program with an error message, but never return failure.
 * They usually have the same arguments and return value as the function they
 * wrap.
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

// Like strncpy but always null terminated.
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

	return ret;
}

// Die unless we can allocate prezeroed memory.
void *xzalloc(size_t size)
{
	void *ret = xmalloc(size);
	bzero(ret,size);
	return ret;
}

// Die unless we can change the size of an existing allocation, possibly
// moving it.  (Notice different arguments from libc function.)
void xrealloc(void **ptr, size_t size)
{
	*ptr = realloc(*ptr, size);
	if (!*ptr) error_exit("xrealloc");
}

// Die unless we can allocate a copy of this string.
void *xstrndup(char *s, size_t n)
{
	void *ret = xmalloc(++n);
	strlcpy(ret, s, n);
	
	return ret;
}

// Die unless we can exec argv[] (or run builtin command).  Note that anything
// with a path isn't a builtin, so /bin/sh won't match the builtin sh.
void *xexec(char **argv)
{
	toy_exec(argv);
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
