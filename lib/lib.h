/* lib.h - header file for lib directory
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

struct ptr_len {
  void *ptr;
  long len;
};

struct str_len {
  char *str;
  long len;
};

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

void llist_free_arg(void *node);
void llist_free_double(void *node);
void llist_traverse(void *list, void (*using)(void *node));
void *llist_pop(void *list);  // actually void **list
void *dlist_pop(void *list);  // actually struct double_list **list
void dlist_add_nomalloc(struct double_list **list, struct double_list *new);
struct double_list *dlist_add(struct double_list **list, char *data);
void *dlist_terminate(void *list);

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
// Don't warn about failure to stat
#define DIRTREE_SHUTUP      16
// Breadth first traversal, conserves filehandles at the expense of memory
#define DIRTREE_BREADTH     32
// Don't look at any more files in this directory.
#define DIRTREE_ABORT      256

#define DIRTREE_ABORTVAL ((struct dirtree *)1)

struct dirtree {
  struct dirtree *next, *parent, *child;
  long extra; // place for user to store their stuff (can be pointer)
  struct stat st;
  char *symlink;
  int dirfd;
  char again;
  char name[];
};

struct dirtree *dirtree_add_node(struct dirtree *p, char *name, int flags);
char *dirtree_path(struct dirtree *node, int *plen);
int dirtree_notdotdot(struct dirtree *catch);
int dirtree_parentfd(struct dirtree *node);
int dirtree_recurse(struct dirtree *node, int (*callback)(struct dirtree *node),
  int symfollow);
struct dirtree *dirtree_flagread(char *path, int flags,
  int (*callback)(struct dirtree *node));
struct dirtree *dirtree_read(char *path, int (*callback)(struct dirtree *node));

// help.c

void show_help(FILE *out);

// xwrap.c
void xstrncpy(char *dest, char *src, size_t size);
void xstrncat(char *dest, char *src, size_t size);
void _xexit(void) noreturn;
void xexit(void) noreturn;
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrndup(char *s, size_t n);
char *xstrdup(char *s);
void *xmemdup(void *s, long len);
char *xmprintf(char *format, ...) printf_format;
void xprintf(char *format, ...) printf_format;
void xputs(char *s);
void xputc(char c);
void xflush(void);
void xexec(char **argv);
pid_t xpopen_both(char **argv, int *pipes);
int xwaitpid(pid_t pid);
int xpclose_both(pid_t pid, int *pipes);
pid_t xpopen(char **argv, int *pipe, int stdout);
pid_t xpclose(pid_t pid, int pipe);
int xrun(char **argv);
int xpspawn(char **argv, int*pipes);
void xaccess(char *path, int flags);
void xunlink(char *path);
int xcreate(char *path, int flags, int mode);
int xopen(char *path, int flags);
void xpipe(int *pp);
void xclose(int fd);
int xdup(int fd);
FILE *xfdopen(int fd, char *mode);
FILE *xfopen(char *path, char *mode);
size_t xread(int fd, void *buf, size_t len);
void xreadall(int fd, void *buf, size_t len);
void xwrite(int fd, void *buf, size_t len);
off_t xlseek(int fd, off_t offset, int whence);
char *xreadfile(char *name, char *buf, off_t len);
int xioctl(int fd, int request, void *data);
char *xgetcwd(void);
void xstat(char *path, struct stat *st);
char *xabspath(char *path, int exact);
void xchdir(char *path);
void xchroot(char *path);
struct passwd *xgetpwuid(uid_t uid);
struct group *xgetgrgid(gid_t gid);
struct passwd *xgetpwnam(char *name);
struct group *xgetgrnam(char *name);
struct passwd *xgetpwnamid(char *user);
struct group *xgetgrnamid(char *group);
void xsetuser(struct passwd *pwd);
char *xreadlink(char *name);
long xparsetime(char *arg, long units, long *fraction);
void xpidfile(char *name);
void xregcomp(regex_t *preg, char *rexec, int cflags);
char *xtzset(char *new);
void xsignal(int signal, void *handler);

// lib.c
void verror_msg(char *msg, int err, va_list va);
void error_msg(char *msg, ...) printf_format;
void perror_msg(char *msg, ...) printf_format;
void error_exit(char *msg, ...) printf_format noreturn;
void perror_exit(char *msg, ...) printf_format noreturn;
void help_exit(char *msg, ...) printf_format noreturn;
void error_msg_raw(char *msg);
void perror_msg_raw(char *msg);
void error_exit_raw(char *msg);
void perror_exit_raw(char *msg);
ssize_t readall(int fd, void *buf, size_t len);
ssize_t writeall(int fd, void *buf, size_t len);
off_t lskip(int fd, off_t offset);
int mkpathat(int atfd, char *dir, mode_t lastmode, int flags);
struct string_list **splitpath(char *path, struct string_list **list);
char *readfileat(int dirfd, char *name, char *buf, off_t *len);
char *readfile(char *name, char *buf, off_t len);
void msleep(long miliseconds);
int64_t peek_le(void *ptr, unsigned size);
int64_t peek_be(void *ptr, unsigned size);
int64_t peek(void *ptr, unsigned size);
void poke(void *ptr, uint64_t val, int size);
struct string_list *find_in_path(char *path, char *filename);
long estrtol(char *str, char **end, int base);
long xstrtol(char *str, char **end, int base);
long atolx(char *c);
long atolx_range(char *numstr, long low, long high);
int stridx(char *haystack, char needle);
char *strlower(char *s);
char *strafter(char *haystack, char *needle);
char *chomp(char *s);
int unescape(char c);
int strstart(char **a, char *b);
off_t fdlength(int fd);
void loopfiles_rw(char **argv, int flags, int permissions, int failok,
  void (*function)(int fd, char *name));
void loopfiles(char **argv, void (*function)(int fd, char *name));
void xsendfile(int in, int out);
int wfchmodat(int rc, char *name, mode_t mode);
int copy_tempfile(int fdin, char *name, char **tempname);
void delete_tempfile(int fdin, int fdout, char **tempname);
void replace_tempfile(int fdin, int fdout, char **tempname);
void crc_init(unsigned int *crc_table, int little_endian);
void base64_init(char *p);
int yesno(int def);
int qstrcmp(const void *a, const void *b);
void create_uuid(char *uuid);
char *show_uuid(char *uuid);
char *next_printf(char *s, char **start);
char *strnstr(char *line, char *str);
int dev_minor(int dev);
int dev_major(int dev);
int dev_makedev(int major, int minor);

#define HR_SPACE 1 // Space between number and units
#define HR_B     2 // Use "B" for single byte units
#define HR_1000  4 // Use decimal instead of binary units
int human_readable(char *buf, unsigned long long num, int style);

// linestack.c

struct linestack {
  long len, max;
  struct ptr_len idx[];
};

void linestack_addstack(struct linestack **lls, struct linestack *throw,
  long pos);
void linestack_insert(struct linestack **lls, long pos, char *line, long len);
void linestack_append(struct linestack **lls, char *line);
struct linestack *linestack_load(char *name);
int crunch_escape(FILE *out, int cols, int wc);
int crunch_rev_escape(FILE *out, int cols, int wc);
int crunch_str(char **str, int width, FILE *out, char *escmore,
  int (*escout)(FILE *out, int cols, int wc));
int draw_str(char *start, int width);
int utf8len(char *str);
int utf8skip(char *str, int width);
int draw_trim_esc(char *str, int padto, int width, char *escmore,
  int (*escout)(FILE *out, int cols,int wc));
int draw_trim(char *str, int padto, int width);

// interestingtimes.c
int xgettty(void);
int terminal_size(unsigned *xx, unsigned *yy);
int terminal_probesize(unsigned *xx, unsigned *yy);
int scan_key_getsize(char *scratch, int miliwait, unsigned *xx, unsigned *yy);
int set_terminal(int fd, int raw, struct termios *old);
void xset_terminal(int fd, int raw, struct termios *old);
int scan_key(char *scratch, int miliwait);
void tty_esc(char *s);
void tty_jump(int x, int y);
void tty_reset(void);
void tty_sigreset(int i);

// net.c
int xsocket(int domain, int type, int protocol);
void xsetsockopt(int fd, int level, int opt, void *val, socklen_t len);
int xconnect(char *host, char *port, int family, int socktype, int protocol,
  int flags);
int xpoll(struct pollfd *fds, int nfds, int timeout);

// password.c
int get_salt(char *salt, char * algo);

// getmountlist.c
struct mtab_list {
  struct mtab_list *next, *prev;
  struct stat stat;
  struct statvfs statvfs;
  char *dir;
  char *device;
  char *opts;
  char type[0];
};

void comma_args(struct arg_list *al, void *data, char *err,
  char *(*callback)(void *data, char *str, int len));
void comma_collate(char **old, char *new);
char *comma_iterate(char **list, int *len);
int comma_scan(char *optlist, char *opt, int clean);
int comma_scanall(char *optlist, char *scanlist);
int mountlist_istype(struct mtab_list  *ml, char *typelist);
struct mtab_list *xgetmountlist(char *path);

// signal

void generic_signal(int signal);
void sigatexit(void *handler);
int sig_to_num(char *pidstr);
char *num_to_sig(int sig);

mode_t string_to_mode(char *mode_str, mode_t base);
void mode_to_string(mode_t mode, char *buf);
char *basename_readonly(char *name);
void names_to_pid(char **names, int (*callback)(pid_t pid, char *name));

pid_t xvforkwrap(pid_t pid);
#define XVFORK() xvforkwrap(vfork())

// Wrapper to make xfuncs() return (via longjmp) instead of exiting.
// Assigns true/false "did it exit" value to first argument.
#define WOULD_EXIT(y, x) do { jmp_buf _noexit; \
  int _noexit_res; \
  toys.rebound = &_noexit; \
  _noexit_res = setjmp(_noexit); \
  if (!_noexit_res) do {x;} while(0); \
  toys.rebound = 0; \
  y = _noexit_res; \
} while(0);

// Wrapper that discards true/false "did it exit" value.
#define NOEXIT(x) WOULD_EXIT(_noexit_res, x)

// Functions in need of further review/cleanup
#include "lib/pending.h"
