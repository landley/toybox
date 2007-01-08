/* vi: set sw=4 ts=4 :*/
/* functions.c - reusable stuff.
 *
 * Functions with the x prefix are wrappers for library functions.  They either
 * succeed or kill the program with an error message, but never return failure.
 * They usually have the same arguments and return value as the function they
 * wrap.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

void verror_msg(char *msg, int err, va_list va)
{
	fprintf(stderr, "%s: ", toys.which->name);
	vfprintf(stderr, msg, va);
	if (err) fprintf(stderr, ": %s", strerror(err));
	putc('\n', stderr);
}

void error_msg(char *msg, ...)
{
	va_list va;

	va_start(va, msg);
	verror_msg(msg, 0, va);
	va_end(va);
}

void perror_msg(char *msg, ...)
{
	va_list va;

	va_start(va, msg);
	verror_msg(msg, errno, va);
	va_end(va);
}

// Die with an error message.
void error_exit(char *msg, ...)
{
	va_list va;

	va_start(va, msg);
	verror_msg(msg, 0, va);
	va_end(va);

	exit(toys.exitval);
}

// Die with an error message and strerror(errno)
void perror_exit(char *msg, ...)
{
	va_list va;

	va_start(va, msg);
	verror_msg(msg, errno, va);
	va_end(va);

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

// Die unless we can allocate a copy of this many bytes of string.
void *xstrndup(char *s, size_t n)
{
	void *ret = xmalloc(++n);
	strlcpy(ret, s, n);
	
	return ret;
}

// Die unless we can allocate a copy of this string.
void *xstrdup(char *s)
{
	return xstrndup(s,strlen(s));
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
void xexec(char **argv)
{
	toy_exec(argv);
	execvp(argv[0], argv);
	error_exit("No %s", argv[0]);
}

void xaccess(char *path, int flags)
{
	if (access(path, flags)) error_exit("Can't access '%s'\n", path);
}

// Die unless we can open/create a file, returning file descriptor.
int xcreate(char *path, int flags, int mode)
{
	int fd = open(path, flags, mode);
	if (fd == -1) error_exit("No file %s\n", path);
	return fd;
}

// Die unless we can open a file, returning file descriptor.
int xopen(char *path, int flags)
{
	return xcreate(path, flags, 0);
}

// Die unless we can open/create a file, returning FILE *.
FILE *xfopen(char *path, char *mode)
{
	FILE *f = fopen(path, mode);
	if (!f) error_exit("No file %s\n", path);
	return f;
}

// Read from file handle, retrying if interrupted.
ssize_t reread(int fd, void *buf, size_t count)
{
	for (;;) {
		ssize_t len = read(fd, buf, count);
		if (len >= 0  || errno != EINTR) return len;
	}
}

// Write to file handle, retrying if interrupted.
ssize_t rewrite(int fd, void *buf, size_t count)
{
	for (;;) {
		ssize_t len = write(fd, buf, count);
		if (len >= 0 || errno != EINTR) return len;
	}
}

// Keep reading until full or EOF
ssize_t readall(int fd, void *buf, size_t count)
{
	size_t len = 0;
	while (len<count) {
		int i = reread(fd, buf, count);
		if (!i) return len;
		if (i<0) return i;
		count += i;
	}

	return count;
}

// Keep writing until done or EOF
ssize_t writeall(int fd, void *buf, size_t count)
{
	size_t len = 0;
	while (len<count) {
		int i = rewrite(fd, buf, count);
		if (!i) return len;
		if (i<0) return i;
		count += i;
	}

	return count;
}

// Die if we can't fill a buffer
void xread(int fd, char *buf, size_t count)
{
	if (count != readall(fd, buf, count)) perror_exit("xread");
}	

void xwrite(int fd, char *buf, size_t count)
{
	if (count != writeall(fd, buf, count)) perror_exit("xwrite");
}

char *xgetcwd(void)
{
	char *buf = getcwd(NULL, 0);
	if (!buf) error_exit("xgetcwd");

	return buf;
}

// Cannonicalizes path by removing ".", "..", and "//" elements.  This is not
// the same as realpath(), where "dir/.." could wind up somewhere else by
// following symlinks.
char *xabspath(char *path)
{
	char *from, *to;

	// If this isn't an absolute path, make it one with cwd.
	if (path[0]!='/') {
		char *cwd=xgetcwd();
		path = xmsprintf("%s/%s",cwd,path);
		free(cwd);
	} else path = xstrdup(path);

	// Loop through path elements
	from = to = path;
	while (*from) {

		// Continue any current path component.
		if (*from!='/') {
			*(to++) = *(from++);
			continue;
		}

		// Skip duplicate slashes.
		while (*from=='/') from++;
		
		// Start of a new filename.  Handle . and ..
		while (*from=='.') {
			// Skip .
			if (from[1]=='/') from += 2;
			else if (!from[1]) from++;
			// Back up for ..
			else if (from[1]=='.') {
				if (from[2]=='/') from +=3;
				else if(!from[2]) from+=2;
				else break;
				while (to>path && *(--to)!='/');
			} else break;
		}
		// Add directory separator slash.
		*(to++) = '/';
	}
	*to = 0;

	return path;
}

// Find all file in a colon-separated path with access type "type" (generally
// X_OK or R_OK).  Returns a list of absolute paths to each file found, in
// order.

struct string_list *find_in_path(char *path, char *filename)
{
	struct string_list *rlist = NULL;
	char *cwd = xgetcwd();

	for (;;) {
		char *next = path ? index(path, ':') : NULL;
		int len = next ? next-path : strlen(path);
		struct string_list *rnext;
		struct stat st;

		rnext = xmalloc(sizeof(void *) + strlen(filename)
			+ (len ? len : strlen(cwd)) + 2);
		if (!len) sprintf(rnext->str, "%s/%s", cwd, filename);
		else {
			char *res = rnext->str;
			strncpy(res, path, len);
			res += len;
			*(res++) = '/';
			strcpy(res, filename);
		}

		// Confirm it's not a directory.
		if (!stat(rnext->str, &st) && S_ISREG(st.st_mode)) {
			rnext->next = rlist;
			rlist = rnext;
		} else free(rnext);

		if (!next) break;
		path += len;
		path++;
	}
	free(cwd);

	return rlist;

}

// Convert unsigned int to ascii, writing into supplied buffer.  A truncated
// result contains the first few digits of the result ala strncpy, and is
// always null terminated (unless buflen is 0).
void utoa_to_buf(unsigned n, char *buf, unsigned buflen)
{
	int i, out = 0;

	if (buflen) {
		for (i=1000000000; i; i/=10) {
			int res = n/i;

			if ((res || out || i == 1) && --buflen>0) {
				out++;
				n -= res*i;
				*buf++ = '0' + res;
			}
		}
		*buf = 0;
	}
}

// Convert signed integer to ascii, using utoa_to_buf()
void itoa_to_buf(int n, char *buf, unsigned buflen)
{
	if (buflen && n<0) {
		n = -n;
		*buf++ = '-';
		buflen--;
	}
	utoa_to_buf((unsigned)n, buf, buflen);
}

// This static buffer is used by both utoa() and itoa(), calling either one a
// second time will overwrite the previous results.
//
// The longest 32 bit integer is -2 billion plus a null terminator: 12 bytes.
// Note that int is always 32 bits on any remotely unix-like system, see
// http://www.unix.org/whitepapers/64bit.html for details.

static char itoa_buf[12];

// Convert unsigned integer to ascii, returning a static buffer.
char *utoa(unsigned n)
{
	utoa_to_buf(n, itoa_buf, sizeof(itoa_buf));

	return itoa_buf;
}

char *itoa(int n)
{
	itoa_to_buf(n, itoa_buf, sizeof(itoa_buf));

	return itoa_buf;
}
