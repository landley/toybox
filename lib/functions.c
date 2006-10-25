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

// Die unless we can allocate enough space to sprintf() into.
char *xmsprintf(char *format, ...)
{
	va_list va;
	int len;
	char *ret;
	
	// How long is it?

	va_start(va, format);
	len = vsnprintf(0, 0, format, va);
	len++;
	va_end(va);

	// Allocate and do the sprintf()
	ret = xmalloc(len);
	va_start(va, format);
	vsnprintf(ret, len, format, va);	
	va_end(va);

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

// int xread(int fd, char *buf, int len)     // Die if can't fill buffer
// int readall(int fd, char *buf, int len)   // Keep reading until full or EOF
// int toy_read(int fd, char *buf, int len)  // retry if interrupted

char *xgetcwd(void)
{
	char *buf = getcwd(NULL, 0);
	if (!buf) error_exit("xgetcwd");
}

// Find this file in a colon-separated path.

char *find_in_path(char *path, char *filename)
{
	char *next, *res = NULL, *cwd = xgetcwd();

	while (next = index(path,':')) {
		int len = next-path;

		if (len==1) res = xmsprintf("%s/%s", cwd, filename);
		else res = xmsprintf("%*s/%s",len-1,path,filename);
		// Is there a file here we can execute?
		if (!access(res, X_OK)) {
			struct stat st;
			// Confirm it's not a directory.
			if (!stat(res, &st) && S_ISREG(st.st_mode)) break;
		}
		free(res);
		res = NULL;
	}
	free(cwd);

	return res;
}
