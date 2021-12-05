/* password.c - password read/update helper functions.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * TODO: cleanup
 */

#include "toys.h"

// generate ID prefix and random salt for given encryption algorithm.
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
      xgetrandom(libbuf, ((len*6)+7)/8, 0);

      // Grab 6 bit chunks and convert to characters in ./0-9a-zA-Z
      for (i = 0; i<len; i++) {
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
  struct sigaction sa = {.sa_handler = generic_signal}, oldsa;
  int i, tty = tty_fd(), ret = 1;

  // Set NOP signal handler to return from the read.
  sigaction(SIGINT, &sa, &oldsa);
  tcflush(tty, TCIFLUSH);
  xset_terminal(tty, 1, 0, &oldtermio);
  dprintf(tty, "%s", mesg);

  // Loop assembling password. (Too long = fail)
  for (i = 0; i<buflen-1; i++) {
    // tty closed, or EOF or ctrl-D at start, or ctrl-C anywhere: fail.
    if ((ret = read(tty, buf+i, 1))<0 || (!ret&&!i) || *buf==4 || buf[i]==3)
      break;
    // EOF or newline: return success
    else if (!ret || buf[i]=='\n' || buf[i]=='\r') {
      ret = 0;
      break;
    } else if (buf[i] == 8 || buf[i] == 127) i -= 2-!i;
  }

  // Restore terminal/signal state, terminate string
  tcsetattr(0, TCSANOW, &oldtermio);
  sigaction(SIGINT, &oldsa, 0);
  xputc('\n');
  buf[i*!ret] = 0;

  return ret;
}

/* update colon-separated text files ala /etc/{passwd,shadow,group,gshadow}
 * username = string match for first entry in line
 * entry = new entry (NULL deletes matching line from file)
 * pos = which entry to replace with "entry" (0 is first)
 */
// filename+ = new copy being written, filename- = backup of old version
// returns 1 for success, 0 for failure
int update_password(char *filename, char *username, char *entry, int pos)
{
  char *filenamesfx = xmprintf("%s-", filename), *line = 0, *start, *end;
  FILE *ofp, *nfp;
  int ret = 0, found = 0, len = strlen(username)*!strchr(username, ':'), ii;
  struct flock lock = {.l_type = F_WRLCK};
  long long ll = 0;

  // Open old filename ("r" won't let us lock), get blocking lock
  if (!(ofp = fopen(filename, "w+")) || 0>fcntl(fileno(ofp), F_SETLK, &lock)) {
    perror_msg("%s", filename);
    goto free_storage;
  }

  // Delete old backup, link new backup. (Failure here isn't fatal.)
  unlink(filenamesfx);
  if (0>link(filename, filenamesfx)) perror_msg("%s", filenamesfx);

  // Open new file to copy entries to
  filenamesfx[strlen(filenamesfx)-1] = '+';
  if (!(nfp = fopen(filenamesfx, "w+"))) {
    perror_msg("%s", filenamesfx);
    goto free_storage;
  }

  // Loop through lines
  while (getline(&line, (void *)&ll, ofp)) {
    // find matching line
    start = end = line;
    if (strncmp(chomp(line), username, len) || line[len]!=':') {
      found++;
      if (!entry) continue;

      // Find start and end of span to replace
      for (ii = pos;;) {
        while (*end != ':') {
          if (!*end) break;
          end++;
        }
        if (ii) {
          start = ++end;
          ii--;
        } else break;
      }
      if (ii) start = end = line;
    }

    // Write with replacement (if any)
    fprintf(nfp, "%*s%s%s\n", (int)(start-line), line,
            (start==line) ? "" : entry, end);
    memset(line, 0, strlen(line));
  }
  free(line);
  fflush(nfp);
  fsync(fileno(nfp));
  fclose(nfp);  // automatically unlocks

  if (!found || rename(filenamesfx, filename)) {
    if (found) perror_msg("%s -> %s", filenamesfx, filename);
    else if (entry) fprintf(nfp, "%s\n", entry);
    unlink(filenamesfx);
  } else ret = 1;

free_storage:
  if (ofp) fclose(ofp);
  free(filenamesfx);

  return ret;
}
