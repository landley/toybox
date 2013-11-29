// pending.h - header for pending.c

// Unfortunately, sizeof() doesn't work in a preprocessor test.  TODO.

//#if sizeof(double) <= sizeof(long)
//typedef double FLOAT;
//#else
typedef float FLOAT;
//#endif

// libc generally has this, but the headers are screwed up
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

void names_to_pid(char **names, int (*callback)(pid_t pid, char *name));

// password.c
#define MAX_SALT_LEN  20 //3 for id, 16 for key, 1 for '\0'
#define SYS_FIRST_ID  100
#define SYS_LAST_ID   999
int get_salt(char *salt, char * algo);
void is_valid_username(const char *name);
int read_password(char * buff, int buflen, char* mesg);
int update_password(char *filename, char* username, char* encrypted);

// cut helper functions
void daemonize(void);
char *human_readable(unsigned long long size);
