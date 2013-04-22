/* lib.h - header file for lib directory
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

// Unfortunately, sizeof() doesn't work in a preprocessor test.  TODO.

//#if sizeof(double) <= sizeof(long)
//typedef double FLOAT;
//#else
typedef float FLOAT;
//#endif

// libc generally has this, but the headers are screwed up
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

// llist.c

// All these list types can be handled by the same code because first element
// is always next pointer, so next = (mytype *)&struct. (The payloads are
// named differently to catch using the wrong type early.)

struct string_list {
  struct string_list *next;
  char str[0];
};

struct arg_list {
  struct arg_list *next;
  char *arg;
};

struct double_list {
  struct double_list *next, *prev;
  char *data;
};

void llist_traverse(void *list, void (*using)(void *data));
void *llist_pop(void *list);  // actually void **list, but the compiler's dumb
void dlist_add_nomalloc(struct double_list **list, struct double_list *new);
struct double_list *dlist_add(struct double_list **list, char *data);

// args.c
void get_optflags(void);

// dirtree.c

// Values returnable from callback function (bitfield, or them together)
// Default with no callback is 0

// Add this node to the tree
#define DIRTREE_SAVE         1
// Recurse into children
#define DIRTREE_RECURSE      2
// Call again after handling all children of this directory
// (Ignored for non-directories, sets linklen = -1 before second call.)
#define DIRTREE_COMEAGAIN    4
// Follow symlinks to directories
#define DIRTREE_SYMFOLLOW    8
// Don't look at any more files in this directory.
#define DIRTREE_ABORT      256

#define DIRTREE_ABORTVAL ((struct dirtree *)1)

struct dirtree {
  struct dirtree *next, *parent, *child;
  long extra; // place for user to store their stuff (can be pointer)
  struct stat st;
  char *symlink;
  int data;  // dirfd for directory, linklen for symlink, -1 = comeagain
  char name[];
};

struct dirtree *dirtree_add_node(struct dirtree *p, char *name, int symfollow);
char *dirtree_path(struct dirtree *node, int *plen);
int dirtree_notdotdot(struct dirtree *catch);
int dirtree_parentfd(struct dirtree *node);
struct dirtree *dirtree_handle_callback(struct dirtree *new,
  int (*callback)(struct dirtree *node));
void dirtree_recurse(struct dirtree *node,
  int (*callback)(struct dirtree *node), int symfollow);
struct dirtree *dirtree_read(char *path, int (*callback)(struct dirtree *node));

// help.c

void show_help(void);

// lib.c
void xstrncpy(char *dest, char *src, size_t size);
void verror_msg(char *msg, int err, va_list va);
void error_msg(char *msg, ...);
void perror_msg(char *msg, ...);
void error_exit(char *msg, ...) noreturn;
void perror_exit(char *msg, ...) noreturn;
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrndup(char *s, size_t n);
char *xstrdup(char *s);
char *xmsprintf(char *format, ...);
void xprintf(char *format, ...);
void xputs(char *s);
void xputc(char c);
void xflush(void);
void xexec(char **argv);
void xaccess(char *path, int flags);
void xunlink(char *path);
int xcreate(char *path, int flags, int mode);
int xopen(char *path, int flags);
void xclose(int fd);
int xdup(int fd);
FILE *xfopen(char *path, char *mode);
ssize_t readall(int fd, void *buf, size_t len);
ssize_t writeall(int fd, void *buf, size_t len);
size_t xread(int fd, void *buf, size_t len);
void xreadall(int fd, void *buf, size_t len);
void xwrite(int fd, void *buf, size_t len);
off_t xlseek(int fd, off_t offset, int whence);
off_t lskip(int fd, off_t offset);
char *readfile(char *name);
char *xreadfile(char *name);
void msleep(long miliseconds);
int xioctl(int fd, int request, void *data);
char *xgetcwd(void);
void xstat(char *path, struct stat *st);
char *xabspath(char *path, int exact);
char *xrealpath(char *path);
void xchdir(char *path);
void xmkpath(char *path, int mode);
void xsetuid(uid_t uid);
struct string_list *find_in_path(char *path, char *filename);
void utoa_to_buf(unsigned n, char *buf, unsigned buflen);
void itoa_to_buf(int n, char *buf, unsigned buflen);
char *utoa(unsigned n);
char *itoa(int n);
long atolx(char *c);
int numlen(long l);
int stridx(char *haystack, char needle);
off_t fdlength(int fd);
char *xreadlink(char *name);
void loopfiles_rw(char **argv, int flags, int permissions, int failok,
  void (*function)(int fd, char *name));
void loopfiles(char **argv, void (*function)(int fd, char *name));
char *get_rawline(int fd, long *plen, char end);
char *get_line(int fd);
void xsendfile(int in, int out);
int wfchmodat(int rc, char *name, mode_t mode);
int copy_tempfile(int fdin, char *name, char **tempname);
void delete_tempfile(int fdin, int fdout, char **tempname);
void replace_tempfile(int fdin, int fdout, char **tempname);
void crc_init(unsigned int *crc_table, int little_endian);
void terminal_size(unsigned *x, unsigned *y);
int yesno(char *prompt, int def);
void for_each_pid_with_name_in(char **names, int (*callback)(pid_t pid, char *name));
unsigned long xstrtoul(const char *nptr, char **endptr, int base);

// net.c
int xsocket(int domain, int type, int protocol);

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

// signal

void sigatexit(void *handler);
int sig_to_num(char *pidstr);
char *num_to_sig(int sig);

mode_t string_to_mode(char *mode_str, mode_t base);
void format_mode(char (*buf)[11], mode_t mode);

// password helper functions
int read_password(char * buff, int buflen, char* mesg);
int update_password(char *filename, char* username, char* encrypted);

// du helper functions
char* make_human_readable(unsigned long long size, unsigned long unit);

// cut helper functions
unsigned long get_int_value(const char *numstr, unsigned lowrange, unsigned highrange);
