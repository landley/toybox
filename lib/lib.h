/* vi: set ts=4 :*/
/* lib.h - header file for lib directory
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

// functions.c
void verror_msg(char *msg, int err, va_list va);
void error_msg(char *msg, ...);
void perror_msg(char *msg, ...);
void error_exit(char *msg, ...);
void perror_exit(char *msg, ...);
void strlcpy(char *dest, char *src, size_t size);
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void xrealloc(void **ptr, size_t size);
void *xstrndup(char *s, size_t n);
char *xmsprintf(char *format, ...);
void xexec(char **argv);
int xopen(char *path, int flags, int mode);
FILE *xfopen(char *path, char *mode);
char *xgetcwd(void);
char *find_in_path(char *path, char *filename);
void utoa_to_buf(unsigned n, char *buf, unsigned buflen);
void itoa_to_buf(int n, char *buf, unsigned buflen);
char *utoa(unsigned n);
char *itoa(int n);

// llist.c
void llist_free(void *list, void (*freeit)(void *data));

struct string_list {
	struct string_list *next;
	char *str;
};

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

