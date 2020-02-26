// Can't trust libc not to leak enviornment variable memory, so...

#include "toys.h"

// In libc, populated by start code, used by getenv() and exec() and friends.
extern char **environ;

// Returns the number of bytes taken by the environment variables. For use
// when calculating the maximum bytes of environment+argument data that can
// be passed to exec for find(1) and xargs(1).
long environ_bytes()
{
  long bytes = sizeof(char *);
  char **ev;

  for (ev = environ; *ev; ev++) bytes += sizeof(char *) + strlen(*ev) + 1;

  return bytes;
}

// This will clear the inherited environment if called first thing.
// Use this instead of envc so we keep track of what needs to be freed.
void xclearenv(void)
{
  if (toys.envc) {
    int i;

    for (i = 0; environ[i]; i++) if (i>=toys.envc) free(environ[i]);
  } else environ = xmalloc(256*sizeof(char *));
  toys.envc = 1;
  *environ = 0;
}

// Frees entries we set earlier. Use with libc getenv but not setenv/putenv.
// if name has an equals and !val, act like putenv (name=val must be malloced!)
// if !val unset name. (Name with = and val is an error)
void xsetmyenv(int *envc, char ***env, char *name, char *val)
{
  unsigned i, len, ec;
  char *new;

  // If we haven't snapshot initial environment state yet, do so now.
  if (!*envc) {
    // envc is size +1 so even if env empty it's nonzero after initialization
    while ((*env)[(*envc)++]);
    memcpy(new = xmalloc(((*envc|0xff)+1)*sizeof(char *)), *env,
      *envc*sizeof(char *));
    *env = (void *)new;
  }

  new = strchr(name, '=');
  if (new) {
    len = new-name;
    if (val) error_exit("xsetenv %s to %s", name, val);
    new = name;
  } else {
    len = strlen(name);
    if (val) new = xmprintf("%s=%s", name, val);
  }

  ec = (*envc)-1;  // compensate for size +1 above
  for (i = 0; (*env)[i]; i++) {
    // Drop old entry, freeing as appropriate. Assumes no duplicates.
    if (!memcmp(name, (*env)[i], len) && (*env)[i][len]=='=') {
      if (i>=ec) free((*env)[i]);
      else {
        // move old entries down, add at end of old data
        *envc = ec--;
        for (; new ? i<ec : !!(*env)[i]; i++) (*env)[i] = (*env)[i+1];
        i = ec;
      }
      break;
    }
  }

  if (!new) return;

  // resize and null terminate if expanding
  if (!(*env)[i]) {
    len = i+1;
    if (!(len&255)) *env = xrealloc(*env, (len+256)*sizeof(char *));
    (*env)[len] = 0;
  }
  (*env)[i] = new;
}

// xsetenv for normal environment (extern variables).
void xsetenv(char *name, char *val)
{
  return xsetmyenv(&toys.envc, &environ, name, val);
}

void xunsetenv(char *name)
{
  if (strchr(name, '=')) error_exit("xunsetenv %s name has =", name);
  xsetenv(name, 0);
}

// reset environment for a user, optionally clearing most of it
void reset_env(struct passwd *p, int clear)
{
  int i;

  if (clear) {
    char *s, *stuff[] = {"TERM", "DISPLAY", "COLORTERM", "XAUTHORITY"};

    for (i=0; i<ARRAY_LEN(stuff); i++)
      stuff[i] = (s = getenv(stuff[i])) ? xmprintf("%s=%s", stuff[i], s) : 0;
    xclearenv();
    for (i=0; i < ARRAY_LEN(stuff); i++) if (stuff[i]) xsetenv(stuff[i], 0);
    if (chdir(p->pw_dir)) {
      perror_msg("chdir %s", p->pw_dir);
      xchdir("/");
    }
  } else {
    char **ev1, **ev2;

    // remove LD_*, IFS, ENV, and BASH_ENV from environment
    for (ev1 = ev2 = environ;;) {
      while (*ev2 && (strstart(ev2, "LD_") || strstart(ev2, "IFS=") ||
        strstart(ev2, "ENV=") || strstart(ev2, "BASH_ENV="))) ev2++;
      if (!(*ev1++ = *ev2++)) break;
    }
  }

  setenv("PATH", _PATH_DEFPATH, 1);
  setenv("HOME", p->pw_dir, 1);
  setenv("SHELL", p->pw_shell, 1);
  setenv("USER", p->pw_name, 1);
  setenv("LOGNAME", p->pw_name, 1);
}
