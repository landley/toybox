// pending.h - header for pending.c

// password.c
#define MAX_SALT_LEN  20 //3 for id, 16 for key, 1 for '\0'
int read_password(char * buff, int buflen, char* mesg);
int update_password(char *filename, char* username, char* encrypted);
