/* vi: set ts=4 :*/
/* lib.h - header file for lib directory
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

// libc generally has this, but the headers are screwed up
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

// llist.c
void llist_free(void *list, void (*freeit)(void *data));
void *llist_pop(void *list);  // actually void **list, but the compiler's dumb

struct string_list {
	struct string_list *next;
	char str[0];
};

struct arg_list {
	struct arg_list *next;
	char *arg;
};

struct dirtree {
	struct dirtree *next, *child, *parent;
	struct stat st;
	char name[];
};

// args.c
void get_optflags(void);

// functions.c
#if !defined(__UCLIBC__) && !defined(__KLIBC__)
void strlcpy(char *dest, char *src, size_t size);
#endif

void verror_msg(char *msg, int err, va_list va);
void error_msg(char *msg, ...);
void perror_msg(char *msg, ...);
void error_exit(char *msg, ...);
void perror_exit(char *msg, ...);
void usage_exit(void);
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void *xstrndup(char *s, size_t n);
void *xstrdup(char *s);
char *xmsprintf(char *format, ...);
void xprintf(char *format, ...);
void xputs(char *s);
void xputc(char c);
void xflush(void);
void xexec(char **argv);
void xaccess(char *path, int flags);
int xcreate(char *path, int flags, int mode);
int xopen(char *path, int flags);
FILE *xfopen(char *path, char *mode);
ssize_t readall(int fd, void *buf, size_t len);
ssize_t writeall(int fd, void *buf, size_t len);
size_t xread(int fd, void *buf, size_t len);
void xreadall(int fd, void *buf, size_t len);
void xwrite(int fd, void *buf, size_t len);
char *xgetcwd(void);
void xstat(char *path, struct stat *st);
char *xabspath(char *path);
struct string_list *find_in_path(char *path, char *filename);
void utoa_to_buf(unsigned n, char *buf, unsigned buflen);
void itoa_to_buf(int n, char *buf, unsigned buflen);
char *utoa(unsigned n);
char *itoa(int n);
long atolx(char *c);
off_t fdlength(int fd);
char *xreadlink(char *name);
struct dirtree *read_dirtree_node(char *path);
struct dirtree *read_dirtree(char *path, struct dirtree *parent);

// getmountlist.c
struct mtab_list {
	struct mtab_list *next;
	struct stat stat;
	struct statvfs statvfs;
	char *dir;
	char *device;
	char type[0];
};

struct mtab_list *getmountlist(int die);

void bunzipStream(int src_fd, int dst_fd);
