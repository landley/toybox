/* password.c - password read/update helper functions.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * TODO: cleanup
 */

#include "toys.h"
#include <time.h>

// generate appropriate random salt string for given encryption algorithm.
int get_salt(char *salt, char *algo)
{
  struct {
    char *type, id, len;
  } al[] = {{"des", 0, 2}, {"md5", 1, 8}, {"sha256", 5, 16}, {"sha512", 6, 16}};
  int i;

  for (i = 0; i < ARRAY_LEN(al); i++) {
    if (!strcmp(algo, al[i].type)) {
      int len = al[i].len;
      char *s = salt;

      if (al[i].id) s += sprintf(s, "$%c$", '0'+al[i].id);

      // Read appropriate number of random bytes for salt
      i = xopenro("/dev/urandom");
      xreadall(i, libbuf, ((len*6)+7)/8);
      close(i);

      // Grab 6 bit chunks and convert to characters in ./0-9a-zA-Z
      for (i=0; i<len; i++) {
        int bitpos = i*6, bits = bitpos/8;

        bits = ((libbuf[i]+(libbuf[i+1]<<8)) >> (bitpos&7)) & 0x3f;
        bits += 46;
        if (bits > 57) bits += 7;
        if (bits > 90) bits += 6;

        s[i] = bits;
      }
      salt[len] = 0;

      return s-salt;
    }
  }

  return -1;
}

// Prompt with mesg, read password into buf, return 0 for success 1 for fail
int read_password(char *buf, int buflen, char *mesg)
{
  struct termios oldtermio;
  struct sigaction sa, oldsa;
  int i, ret = 1;

  // NOP signal handler to return from the read
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = generic_signal;
  sigaction(SIGINT, &sa, &oldsa);

  tcflush(0, TCIFLUSH);
  set_terminal(0, 1, &oldtermio);

  xprintf("%s", mesg);

  for (i=0; i < buflen-1; i++) {
    if ((ret = read(0, buf+i, 1)) < 0 || (!ret && !i)) {
      i = 0;
      ret = 1;

      break;
    } else if (!ret || buf[i] == '\n' || buf[i] == '\r') {
      ret = 0;

      break;
    } else if (buf[i] == 8 || buf[i] == 127) i -= i ? 2 : 1;
  }

  // Restore terminal/signal state, terminate string
  sigaction(SIGINT, &oldsa, NULL);
  tcsetattr(0, TCSANOW, &oldtermio);
  buf[i] = 0;
  xputc('\n');

  return ret;
}

static char *get_nextcolon(char *line, int cnt)
{
  while (cnt--) {
    if (!(line = strchr(line, ':'))) error_exit("Invalid Entry\n");
    line++; //jump past the colon
  }
  return line;
}

/*update_password is used by multiple utilities to update /etc/passwd,
 * /etc/shadow, /etc/group and /etc/gshadow files,
 * which are used as user, group databeses
 * entry can be
 * 1. encrypted password, when updating user password.
 * 2. complete entry for user details, when creating new user
 * 3. group members comma',' separated list, when adding user to group
 * 4. complete entry for group details, when creating new group
 * 5. entry = NULL, delete the named entry user/group
 */
int update_password(char *filename, char* username, char* entry)
{
  char *filenamesfx = NULL, *namesfx = NULL, *shadow = NULL,
       *sfx = NULL, *line = NULL;
  FILE *exfp, *newfp;
  int ret = -1, found = 0;
  struct flock lock;

  shadow = strstr(filename, "shadow");
  filenamesfx = xmprintf("%s+", filename);
  sfx = strchr(filenamesfx, '+');

  exfp = fopen(filename, "r+");
  if (!exfp) {
    perror_msg("Couldn't open file %s",filename);
    goto free_storage;
  }

  *sfx = '-';
  unlink(filenamesfx);
  ret = link(filename, filenamesfx);
  if (ret < 0) error_msg("can't create backup file");

  *sfx = '+';
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  ret = fcntl(fileno(exfp), F_SETLK, &lock);
  if (ret < 0) perror_msg("Couldn't lock file %s",filename);

  lock.l_type = F_UNLCK; //unlocking at a later stage

  newfp = fopen(filenamesfx, "w+");
  if (!newfp) {
    error_msg("couldn't open file for writing");
    ret = -1;
    fclose(exfp);
    goto free_storage;
  }

  ret = 0;
  namesfx = xmprintf("%s:",username);
  while ((line = get_line(fileno(exfp))) != NULL)
  {
    if (strncmp(line, namesfx, strlen(namesfx)))
      fprintf(newfp, "%s\n", line);
    else if (entry) {
      char *current_ptr = NULL;

      found = 1;
      if (!strcmp(toys.which->name, "passwd")) {
        fprintf(newfp, "%s%s:",namesfx, entry);
        current_ptr = get_nextcolon(line, 2); //past passwd
        if (shadow) {
          fprintf(newfp, "%u:",(unsigned)(time(NULL))/(24*60*60));
          current_ptr = get_nextcolon(current_ptr, 1);
          fprintf(newfp, "%s\n",current_ptr);
        } else fprintf(newfp, "%s\n",current_ptr);
      } else if (!strcmp(toys.which->name, "groupadd") ||
          !strcmp(toys.which->name, "addgroup") ||
          !strcmp(toys.which->name, "delgroup") ||
          !strcmp(toys.which->name, "groupdel")){
        current_ptr = get_nextcolon(line, 3); //past gid/admin list
        *current_ptr = '\0';
        fprintf(newfp, "%s", line);
        fprintf(newfp, "%s\n", entry);
      }
    }
    free(line);
  }
  free(namesfx);
  if (!found && entry) fprintf(newfp, "%s\n", entry);
  fcntl(fileno(exfp), F_SETLK, &lock);
  fclose(exfp);

  errno = 0;
  fflush(newfp);
  fsync(fileno(newfp));
  fclose(newfp);
  rename(filenamesfx, filename);
  if (errno) {
    perror_msg("File Writing/Saving failed: ");
    unlink(filenamesfx);
    ret = -1;
  }

free_storage:
  free(filenamesfx);
  return ret;
}
