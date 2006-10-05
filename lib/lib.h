/* vi: set ts=4 :*/

void error_exit(char *msg, ...);
void strlcpy(char *dest, char *src, size_t size);
void *xmalloc(size_t size);
void *xstrndup(char *s, size_t n);
void *xexec(char **argv);
int xopen(char *path, int flags, int mode);
FILE *xfopen(char *path, char *mode);

struct mtab_list {
	struct mtab_list *next;
	char *dir;
	char *device;
	char type[0];
};

struct mtab_list *getmountlist(int die);

