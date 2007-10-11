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

#if !defined(__UCLIBC__) && !defined(__KLIBC__)

// uClibc has this, and if we define our own it conflicts.

// Like strncpy but always null terminated.
void strlcpy(char *dest, char *src, size_t size)
{
	strncpy(dest,src,size);
	dest[size-1] = 0;
}
#endif


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

	if (CFG_HELP && toys.exithelp) {
		*toys.optargs=*toys.argv;
		help_main();
		fprintf(stderr,"\n");
	}

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
void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr) error_exit("xrealloc");

	return ptr;
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
	va_list va, va2;
	int len;
	char *ret;
	
	va_start(va, format);
	va_copy(va2, va);

	// How long is it?
	len = vsnprintf(0, 0, format, va);
	len++;
	va_end(va);

	// Allocate and do the sprintf()
	ret = xmalloc(len);
	vsnprintf(ret, len, format, va2);	
	va_end(va2);

	return ret;
}

void xprintf(char *format, ...)
{
	va_list va;
	va_start(va, format);

	vprintf(format, va);
	if (ferror(stdout)) perror_exit("write");
}

void xputs(char *s)
{
	if (EOF == puts(s)) perror_exit("write");
}

void xputc(char c)
{
	if (EOF == fputc(c, stdout)) perror_exit("write");
}

void xflush(void)
{
	if (fflush(stdout)) perror_exit("write");;
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
	if (access(path, flags)) perror_exit("Can't access '%s'\n", path);
}

// Die unless we can open/create a file, returning file descriptor.
int xcreate(char *path, int flags, int mode)
{
	int fd = open(path, flags, mode);
	if (fd == -1) perror_exit("No file %s\n", path);
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
	if (!f) perror_exit("No file %s\n", path);
	return f;
}

// Keep reading until full or EOF
ssize_t readall(int fd, void *buf, size_t len)
{
	size_t count = 0;
	while (count<len) {
		int i = read(fd, buf+count, len-count);
		if (!i) return len;
		if (i<0) return i;
		count += i;
	}

	return count;
}

// Keep writing until done or EOF
ssize_t writeall(int fd, void *buf, size_t len)
{
	size_t count = 0;
	while (count<len) {
		int i = write(fd, buf+count, len-count);
		if (i<1) return i;
		count += i;
	}

	return count;
}

// Die if there's an error other than EOF.
size_t xread(int fd, void *buf, size_t len)
{
	len = read(fd, buf, len);
	if (len < 0) perror_exit("xread");

	return len;
}

void xreadall(int fd, void *buf, size_t len)
{
	if (len != readall(fd, buf, len)) perror_exit("xreadall");
}

// There's no xwriteall(), just xwrite().  When we read, there may or may not
// be more data waiting.  When we write, there is data and it had better go
// somewhere.

void xwrite(int fd, void *buf, size_t len)
{
	if (len != writeall(fd, buf, len)) perror_exit("xwrite");
}

char *xgetcwd(void)
{
	char *buf = getcwd(NULL, 0);
	if (!buf) perror_exit("xgetcwd");

	return buf;
}

void xstat(char *path, struct stat *st)
{
	if(stat(path, st)) perror_exit("Can't stat %s\n",path);
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

// atol() with the kilo/mega/giga/tera/peta/exa extensions.
// (zetta and yotta don't fit in 64 bits.)
long atolx(char *c)
{
	char *suffixes="kmgtpe", *end;
	long val = strtol(c, &c, 0);

	end = strchr(suffixes, tolower(*c));
	if (end) val *= 1024<<((end-suffixes)*10);
	return val;
}

// Return how long the file at fd is, if there's any way to determine it.
off_t fdlength(int fd)
{
	off_t bottom = 0, top = 0, pos;
	int size;

	// If the ioctl works for this, return it.

	if (ioctl(fd, BLKGETSIZE, &size) >= 0) return size*512L;

	// If not, do a binary search for the last location we can read.  (Some
	// block devices don't do BLKGETSIZE right.)  This should probably have
	// a CONFIG option...

	do {
		char temp;

		pos = bottom + (top - bottom) / 2;

		// If we can read from the current location, it's bigger.

		if (lseek(fd, pos, 0)>=0 && read(fd, &temp, 1)==1) {
			if (bottom == top) bottom = top = (top+1) * 2;
			else bottom = pos;

			// If we can't, it's smaller.

		} else {
			if (bottom == top) {
				if (!top) return 0;
				bottom = top/2;
			} else top = pos;
		}
	} while (bottom + 1 != top);

	return pos + 1;
}

// This can return null (meaning file not found).  It just won't return null
// for memory allocation reasons.
char *xreadlink(char *name)
{
	int len, size = 0;
	char *buf = 0;

	// Grow by 64 byte chunks until it's big enough.
	for(;;) {
		size +=64;
		buf = xrealloc(buf, size);
		len = readlink(name, buf, size);

		if (len<0) {
			free(buf);
			return 0;
		}
		if (len<size) {
			buf[len]=0;
			return buf;
		}
	}
}

/*
 This might be of use or might not.  Unknown yet...

// Read contents of file as a single freshly allocated nul-terminated string.
char *readfile(char *name)
{
	off_t len;
	int fd;
	char *buf;

	fd = open(pidfile, O_RDONLY);
	if (fd == -1) return 0;
	len = fdlength(fd);
	buf = xmalloc(len+1);
	buf[xread(fd, buf, len)] = 0;

	return buf;
}

char *xreadfile(char *name)
{
	char *buf = readfile(name);
	if (!buf) perror_exit("xreadfile %s", name);
	return buf;
}

*/

// Open a /var/run/NAME.pid file, dying if we can't write it or if it currently
// exists and is this executable.
void xpidfile(char *name)
{
	char pidfile[256], spid[32];
	int i, fd;
	pid_t pid;
	
	sprintf(pidfile, "/var/run/%s.pid", name);
	// Try three times to open the sucker.
	for (i=0; i<3; i++) {
		fd = open(pidfile, O_CREAT|O_EXCL, 0644);
		if (fd != -1) break;

		// If it already existed, read it.  Loop for race condition.
		fd = open(pidfile, O_RDONLY);
		if (fd == -1) continue;

		// Is the old program still there?
		spid[xread(fd, spid, sizeof(spid)-1)] = 0;
		close(fd);
		pid = atoi(spid);
		if (fd < 1 || kill(pid, 0) == ESRCH) unlink(pidfile);

		// An else with more sanity checking might be nice here.
	}
	
	if (i == 3) error_exit("xpidfile %s", name);

	xwrite(fd, spid, sprintf(spid, "%ld\n", (long)getpid()));
	close(fd);
}
