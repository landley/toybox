/* vi: set sw=4 ts=4 :                                                                                                                                                          
 * pwdutils.c - password read/update helper functions.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 */

#include "toys.h"
#include <time.h>


int read_passwd(char * buff, int buflen, char* mesg)
{           
    int i = 0;
    struct termios termio, oldtermio;
    tcgetattr(0, &oldtermio);
    tcflush(0, TCIFLUSH);
    termio = oldtermio;

    termio.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
    termio.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP);
    tcsetattr(0, TCSANOW, &termio);

    fputs(mesg, stdout);
    fflush(stdout);

    while (1) {
        int ret = read(0, &buff[i], 1);
        if ( ret < 0 ) {   
            buff[0] = 0;
            tcsetattr(0, TCSANOW, &oldtermio);
            return 1;
        }   
        else if ( ret == 0 || buff[i] == '\n' ||
                buff[i] == '\r' || buflen == i+1) {   
            buff[i] = '\0';
            break;
        }   
        i++;
    }       

    tcsetattr(0, TCSANOW, &oldtermio);
    puts("");
    fflush(stdout);
    return 0;
}

static char *get_nextcolon(const char *line, char delim)
{
    char *current_ptr = NULL;
    if((current_ptr = strchr(line, ':')) == NULL) 
        error_exit("Invalid Entry\n");      
    return current_ptr;
}

int update_passwd(char *filename, char* username, char* encrypted)
{
    char *filenamesfx = NULL, *namesfx = NULL;
    char *shadow = NULL, *sfx = NULL;
    FILE *exfp, *newfp;
    int ret = -1; //fail
    struct flock lock;
    char *line = NULL;

    shadow = strstr(filename, "shadow");
    filenamesfx = xmsprintf("%s+", filename);
    sfx = strchr(filenamesfx, '+');

    exfp = fopen(filename, "r+");
    if(!exfp) {      
        perror_msg("Couldn't open file %s",filename);
        goto free_storage;
    }

    *sfx = '-';
    ret = unlink(filenamesfx);
    ret = link(filename, filenamesfx);
    if(ret < 0) error_msg("can't create backup file");

    *sfx = '+';
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    ret = fcntl(fileno(exfp), F_SETLK, &lock);
    if(ret < 0) perror_msg("Couldn't lock file %s",filename);

    lock.l_type = F_UNLCK; //unlocking at a later stage                                                                                                                         

    newfp = fopen(filenamesfx, "w+");
    if(!newfp) {       
        error_msg("couldn't open file for writing");
        ret = -1;
        fclose(exfp);
        goto free_storage;
    }       

    ret = 0;
    namesfx = xmsprintf("%s:",username);
    while((line = get_line(fileno(exfp))) != NULL)
    {
        if(strncmp(line, namesfx, strlen(namesfx)) != 0)
            fprintf(newfp, "%s\n", line);
        else {
            char *current_ptr = NULL;
            fprintf(newfp, "%s%s:",namesfx,encrypted);
            current_ptr = get_nextcolon(line, ':'); //past username
            current_ptr++; //past colon ':' after username
            current_ptr = get_nextcolon(current_ptr, ':'); //past passwd
            current_ptr++; //past colon ':' after passwd
            if(shadow) {
                fprintf(newfp, "%u:",(unsigned)(time(NULL))/(24*60*60));
                current_ptr = get_nextcolon(current_ptr, ':'); 
                current_ptr++; //past time stamp colon.
                fprintf(newfp, "%s\n",current_ptr);
            }
            else {
                fprintf(newfp, "%s\n",current_ptr);
            }
        }

        free(line);
    }
    free(namesfx);
    fcntl(fileno(exfp), F_SETLK, &lock);
    fclose(exfp);
    
    errno = 0;
    fflush(newfp);
    fsync(fileno(newfp));
    fclose(newfp);
    rename(filenamesfx, filename);
    if(errno) {
        perror_msg("File Writing/Saving failed: ");
        unlink(filenamesfx);
        ret = -1;
    }      

free_storage:
    free(filenamesfx);
    return ret;
} 
