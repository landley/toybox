/* xwrap.c - wrappers around existing library functions.
 *
 * Functions with the x prefix are wrappers that either succeed or kill the
 * program with an error message, but never return failure. They usually have
 * the same arguments and return value as the function they wrap.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// strcpy and strncat with size checking. Size is the total space in "dest",
// including null terminator. Exit if there's not enough space for the string
// (including space for the null terminator), because silently truncating is
// still broken behavior. (And leaving the string unterminated is INSANE.)
void xstrncpy(char *dest, char *src, size_t size)
{
  if (strlen(src)+1 > size) error_exit("'%s' > %ld bytes", src, (long)size);
  strcpy(dest, src);
}

void xstrncat(char *dest, char *src, size_t size)
{
  long len = strlen(dest);

  if (len+strlen(src)+1 > size)
    error_exit("'%s%s' > %ld bytes", dest, src, (long)size);
  strcpy(dest+len, src);
}

// We replaced exit(), _exit(), and atexit() with xexit(), _xexit(), and
// sigatexit(). This gives _xexit() the option to siglongjmp(toys.rebound, 1)
// instead of exiting, lets xexit() report stdout flush failures to stderr
// and change the exit code to indicate error, lets our toys.exit function
// change happen for signal exit paths and lets us remove the functions
// after we've called them.

void _xexit(void)
{
  if (toys.rebound) siglongjmp(*toys.rebound, 1);

  _exit(toys.exitval);
}

void xexit(void)
{
  // Call toys.xexit functions in reverse order added.
  while (toys.xexit) {
    // This is typecasting xexit->arg to a function pointer,then calling it.
    // Using the invalid signal number 0 lets the signal handlers distinguish
    // an actual signal from a regular exit.
    ((void (*)(int))(toys.xexit->arg))(0);

    free(llist_pop(&toys.xexit));
  }
  if (fflush(NULL) || ferror(stdout))
    if (!toys.exitval) perror_msg("write");
  _xexit();
}

void *xmmap(void *addr, size_t length, int prot, int flags, int fd, off_t off)
{
  void *ret = mmap(addr, length, prot, flags, fd, off);
  if (ret == MAP_FAILED) perror_exit("mmap");
  return ret;
}

// Die unless we can allocate memory.
void *xmalloc(size_t size)
{
  void *ret = malloc(size);
  if (!ret) error_exit("xmalloc(%ld)", (long)size);

  return ret;
}

// Die unless we can allocate prezeroed memory.
void *xzalloc(size_t size)
{
  void *ret = xmalloc(size);
  memset(ret, 0, size);
  return ret;
}

// Die unless we can change the size of an existing allocation, possibly
// moving it.  (Notice different arguments from libc function.)
void *xrealloc(void *ptr, size_t size)
{
  ptr = realloc(ptr, size);
  if (!ptr) error_exit("xrealloc");

  return ptr;
}

// Die unless we can allocate a copy of this many bytes of string.
char *xstrndup(char *s, size_t n)
{
  char *ret = strndup(s, ++n);

  if (!ret) error_exit("xstrndup");
  ret[--n] = 0;

  return ret;
}

// Die unless we can allocate a copy of this string.
char *xstrdup(char *s)
{
  return xstrndup(s, strlen(s));
}

void *xmemdup(void *s, long len)
{
  void *ret = xmalloc(len);
  memcpy(ret, s, len);

  return ret;
}

// Die unless we can allocate enough space to sprintf() into.
char *xmprintf(char *format, ...)
{
  va_list va, va2;
  int len;
  char *ret;

  va_start(va, format);
  va_copy(va2, va);

  // How long is it?
  len = vsnprintf(0, 0, format, va);
  len++;
  va_end(va);

  // Allocate and do the sprintf()
  ret = xmalloc(len);
  vsnprintf(ret, len, format, va2);
  va_end(va2);

  return ret;
}

void xprintf(char *format, ...)
{
  va_list va;
  va_start(va, format);

  vprintf(format, va);
  va_end(va);
  if (fflush(stdout) || ferror(stdout)) perror_exit("write");
}

void xputs(char *s)
{
  if (EOF == puts(s) || fflush(stdout) || ferror(stdout)) perror_exit("write");
}

void xputc(char c)
{
  if (EOF == fputc(c, stdout) || fflush(stdout) || ferror(stdout))
    perror_exit("write");
}

void xflush(void)
{
  if (fflush(stdout) || ferror(stdout)) perror_exit("write");;
}

// This is called through the XVFORK macro because parent/child of vfork
// share a stack, so child returning from a function would stomp the return
// address parent would need. Solution: make vfork() an argument so processes
// diverge before function gets called.
pid_t __attribute__((returns_twice)) xvforkwrap(pid_t pid)
{
  if (pid == -1) perror_exit("vfork");

  // Signal to xexec() and friends that we vforked so can't recurse
  toys.stacktop = 0;

  return pid;
}

// Die unless we can exec argv[] (or run builtin command).  Note that anything
// with a path isn't a builtin, so /bin/sh won't match the builtin sh.
void xexec(char **argv)
{
  // Only recurse to builtin when we have multiplexer and !vfork context.
  if (CFG_TOYBOX && !CFG_TOYBOX_NORECURSE && toys.stacktop) toy_exec(argv);
  execvp(argv[0], argv);

  perror_msg("exec %s", argv[0]);
  toys.exitval = 127;
  if (!CFG_TOYBOX_FORK) _exit(toys.exitval);
  xexit();
}

// Spawn child process, capturing stdin/stdout.
// argv[]: command to exec. If null, child re-runs original program with
//         toys.stacktop zeroed.
// pipes[2]: stdin, stdout of new process, only allocated if zero on way in,
//           pass NULL to skip pipe allocation entirely.
// return: pid of child process
pid_t xpopen_both(char **argv, int *pipes)
{
  int cestnepasun[4], pid;

  // Make the pipes? Note this won't set either pipe to 0 because if fds are
  // allocated in order and if fd0 was free it would go to cestnepasun[0]
  if (pipes) {
    for (pid = 0; pid < 2; pid++) {
      if (pipes[pid] != 0) continue;
      if (pipe(cestnepasun+(2*pid))) perror_exit("pipe");
      pipes[pid] = cestnepasun[pid+1];
    }
  }

  // Child process.
  if (!(pid = CFG_TOYBOX_FORK ? xfork() : XVFORK())) {
    // Dance of the stdin/stdout redirection.
    if (pipes) {
      // if we had no stdin/out, pipe handles could overlap, so test for it
      // and free up potentially overlapping pipe handles before reuse
      if (pipes[1] != -1) close(cestnepasun[2]);
      if (pipes[0] != -1) {
        close(cestnepasun[1]);
        if (cestnepasun[0]) {
          dup2(cestnepasun[0], 0);
          close(cestnepasun[0]);
        }
      }
      if (pipes[1] != -1) {
        dup2(cestnepasun[3], 1);
        dup2(cestnepasun[3], 2);
        if (cestnepasun[3] > 2 || !cestnepasun[3]) close(cestnepasun[3]);
      }
    }
    if (argv) xexec(argv);

    // In fork() case, force recursion because we know it's us.
    if (CFG_TOYBOX_FORK) {
      toy_init(toys.which, toys.argv);
      toys.stacktop = 0;
      toys.which->toy_main();
      xexit();
    // In vfork() case, exec /proc/self/exe with high bit of first letter set
    // to tell main() we reentered.
    } else {
      char *s = "/proc/self/exe";

      // We did a nommu-friendly vfork but must exec to continue.
      // setting high bit of argv[0][0] to let new process know
      **toys.argv |= 0x80;
      execv(s, toys.argv);
      perror_msg_raw(s);

      _exit(127);
    }
  }

  // Parent process
  if (!CFG_TOYBOX_FORK) **toys.argv &= 0x7f;
  if (pipes) {
    if (pipes[0] != -1) close(cestnepasun[0]);
    if (pipes[1] != -1) close(cestnepasun[3]);
  }

  return pid;
}

// Wait for child process to exit, then return adjusted exit code.
int xwaitpid(pid_t pid)
{
  int status;

  while (-1 == waitpid(pid, &status, 0) && errno == EINTR);

  return WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status)+127;
}

int xpclose_both(pid_t pid, int *pipes)
{
  if (pipes) {
    close(pipes[0]);
    close(pipes[1]);
  }

  return xwaitpid(pid);
}

// Wrapper to xpopen with a pipe for just one of stdin/stdout
pid_t xpopen(char **argv, int *pipe, int isstdout)
{
  int pipes[2], pid;

  pipes[!isstdout] = -1;
  pipes[!!isstdout] = 0;
  pid = xpopen_both(argv, pipes);
  *pipe = pid ? pipes[!!isstdout] : -1;

  return pid;
}

int xpclose(pid_t pid, int pipe)
{
  close(pipe);

  return xpclose_both(pid, 0);
}

// Call xpopen and wait for it to finish, keeping existing stdin/stdout.
int xrun(char **argv)
{
  return xpclose_both(xpopen_both(argv, 0), 0);
}

void xaccess(char *path, int flags)
{
  if (access(path, flags)) perror_exit("Can't access '%s'", path);
}

// Die unless we can delete a file.  (File must exist to be deleted.)
void xunlink(char *path)
{
  if (unlink(path)) perror_exit("unlink '%s'", path);
}

// Die unless we can open/create a file, returning file descriptor.
// The meaning of O_CLOEXEC is reversed (it defaults on, pass it to disable)
// and WARN_ONLY tells us not to exit.
int xcreate_stdio(char *path, int flags, int mode)
{
  int fd = open(path, (flags^O_CLOEXEC)&~WARN_ONLY, mode);

  if (fd == -1) ((mode&WARN_ONLY) ? perror_msg_raw : perror_exit_raw)(path);
  return fd;
}

// Die unless we can open a file, returning file descriptor.
int xopen_stdio(char *path, int flags)
{
  return xcreate_stdio(path, flags, 0);
}

void xpipe(int *pp)
{
  if (pipe(pp)) perror_exit("xpipe");
}

void xclose(int fd)
{
  if (close(fd)) perror_exit("xclose");
}

int xdup(int fd)
{
  if (fd != -1) {
    fd = dup(fd);
    if (fd == -1) perror_exit("xdup");
  }
  return fd;
}

// Move file descriptor above stdin/stdout/stderr, using /dev/null to consume
// old one. (We should never be called with stdin/stdout/stderr closed, but...)
int notstdio(int fd)
{
  if (fd<0) return fd;

  while (fd<3) {
    int fd2 = xdup(fd);

    close(fd);
    xopen_stdio("/dev/null", O_RDWR);
    fd = fd2;
  }

  return fd;
}

// Create a file but don't return stdin/stdout/stderr
int xcreate(char *path, int flags, int mode)
{
  return notstdio(xcreate_stdio(path, flags, mode));
}

// Open a file descriptor NOT in stdin/stdout/stderr
int xopen(char *path, int flags)
{
  return notstdio(xopen_stdio(path, flags));
}

// Open read only, treating "-" as a synonym for stdin, defaulting to warn only
int openro(char *path, int flags)
{
  if (!strcmp(path, "-")) return 0;

  return xopen(path, flags^WARN_ONLY);
}

// Open read only, treating "-" as a synonym for stdin.
int xopenro(char *path)
{
  return openro(path, O_RDONLY|WARN_ONLY);
}

FILE *xfdopen(int fd, char *mode)
{
  FILE *f = fdopen(fd, mode);

  if (!f) perror_exit("xfdopen");

  return f;
}

// Die unless we can open/create a file, returning FILE *.
FILE *xfopen(char *path, char *mode)
{
  FILE *f = fopen(path, mode);
  if (!f) perror_exit("No file %s", path);
  return f;
}

// Die if there's an error other than EOF.
size_t xread(int fd, void *buf, size_t len)
{
  ssize_t ret = read(fd, buf, len);
  if (ret < 0) perror_exit("xread");

  return ret;
}

void xreadall(int fd, void *buf, size_t len)
{
  if (len != readall(fd, buf, len)) perror_exit("xreadall");
}

// There's no xwriteall(), just xwrite().  When we read, there may or may not
// be more data waiting.  When we write, there is data and it had better go
// somewhere.

void xwrite(int fd, void *buf, size_t len)
{
  if (len != writeall(fd, buf, len)) perror_exit("xwrite");
}

// Die if lseek fails, probably due to being called on a pipe.

off_t xlseek(int fd, off_t offset, int whence)
{
  offset = lseek(fd, offset, whence);
  if (offset<0) perror_exit("lseek");

  return offset;
}

char *xgetcwd(void)
{
  char *buf = getcwd(NULL, 0);
  if (!buf) perror_exit("xgetcwd");

  return buf;
}

void xstat(char *path, struct stat *st)
{
  if(stat(path, st)) perror_exit("Can't stat %s", path);
}

// Cannonicalize path, even to file with one or more missing components at end.
// if exact, require last path component to exist
char *xabspath(char *path, int exact)
{
  struct string_list *todo, *done = 0;
  int try = 9999, dirfd = open("/", 0);;
  char *ret;

  // If this isn't an absolute path, start with cwd.
  if (*path != '/') {
    char *temp = xgetcwd();

    splitpath(path, splitpath(temp, &todo));
    free(temp);
  } else splitpath(path, &todo);

  // Iterate through path components
  while (todo) {
    struct string_list *new = llist_pop(&todo), **tail;
    ssize_t len;

    if (!try--) {
      errno = ELOOP;
      goto error;
    }

    // Removable path componenents.
    if (!strcmp(new->str, ".") || !strcmp(new->str, "..")) {
      int x = new->str[1];

      free(new);
      if (x) {
        if (done) free(llist_pop(&done));
        len = 0;
      } else continue;

    // Is this a symlink?
    } else len = readlinkat(dirfd, new->str, libbuf, sizeof(libbuf));

    if (len>4095) goto error;
    if (len<1) {
      int fd;
      char *s = "..";

      // For .. just move dirfd
      if (len) {
        // Not a symlink: add to linked list, move dirfd, fail if error
        if ((exact || todo) && errno != EINVAL) goto error;
        new->next = done;
        done = new;
        if (errno == EINVAL && !todo) break;
        s = new->str;
      }
      fd = openat(dirfd, s, 0);
      if (fd == -1 && (exact || todo || errno != ENOENT)) goto error;
      close(dirfd);
      dirfd = fd;
      continue;
    }

    // If this symlink is to an absolute path, discard existing resolved path
    libbuf[len] = 0;
    if (*libbuf == '/') {
      llist_traverse(done, free);
      done=0;
      close(dirfd);
      dirfd = open("/", 0);
    }
    free(new);

    // prepend components of new path. Note symlink to "/" will leave new NULL
    tail = splitpath(libbuf, &new);

    // symlink to "/" will return null and leave tail alone
    if (new) {
      *tail = todo;
      todo = new;
    }
  }
  close(dirfd);

  // At this point done has the path, in reverse order. Reverse list while
  // calculating buffer length.

  try = 2;
  while (done) {
    struct string_list *temp = llist_pop(&done);;

    if (todo) try++;
    try += strlen(temp->str);
    temp->next = todo;
    todo = temp;
  }

  // Assemble return buffer

  ret = xmalloc(try);
  *ret = '/';
  ret [try = 1] = 0;
  while (todo) {
    if (try>1) ret[try++] = '/';
    try = stpcpy(ret+try, todo->str) - ret;
    free(llist_pop(&todo));
  }

  return ret;

error:
  close(dirfd);
  llist_traverse(todo, free);
  llist_traverse(done, free);

  return NULL;
}

void xchdir(char *path)
{
  if (chdir(path)) error_exit("chdir '%s'", path);
}

void xchroot(char *path)
{
  if (chroot(path)) error_exit("chroot '%s'", path);
  xchdir("/");
}

struct passwd *xgetpwuid(uid_t uid)
{
  struct passwd *pwd = getpwuid(uid);
  if (!pwd) error_exit("bad uid %ld", (long)uid);
  return pwd;
}

struct group *xgetgrgid(gid_t gid)
{
  struct group *group = getgrgid(gid);

  if (!group) perror_exit("gid %ld", (long)gid);
  return group;
}

unsigned xgetuid(char *name)
{
  struct passwd *up = getpwnam(name);
  char *s = 0;
  long uid;

  if (up) return up->pw_uid;

  uid = estrtol(name, &s, 10);
  if (!errno && s && !*s && uid>=0 && uid<=UINT_MAX) return uid;

  error_exit("bad user '%s'", name);
}

unsigned xgetgid(char *name)
{
  struct group *gr = getgrnam(name);
  char *s = 0;
  long gid;

  if (gr) return gr->gr_gid;

  gid = estrtol(name, &s, 10);
  if (!errno && s && !*s && gid>=0 && gid<=UINT_MAX) return gid;

  error_exit("bad group '%s'", name);
}

struct passwd *xgetpwnam(char *name)
{
  struct passwd *up = getpwnam(name);

  if (!up) perror_exit("user '%s'", name);
  return up;
}

struct group *xgetgrnam(char *name)
{
  struct group *gr = getgrnam(name);

  if (!gr) perror_exit("group '%s'", name);
  return gr;
}

// setuid() can fail (for example, too many processes belonging to that user),
// which opens a security hole if the process continues as the original user.

void xsetuser(struct passwd *pwd)
{
  if (initgroups(pwd->pw_name, pwd->pw_gid) || setgid(pwd->pw_uid)
      || setuid(pwd->pw_uid)) perror_exit("xsetuser '%s'", pwd->pw_name);
}

// This can return null (meaning file not found).  It just won't return null
// for memory allocation reasons.
char *xreadlink(char *name)
{
  int len, size = 0;
  char *buf = 0;

  // Grow by 64 byte chunks until it's big enough.
  for(;;) {
    size +=64;
    buf = xrealloc(buf, size);
    len = readlink(name, buf, size);

    if (len<0) {
      free(buf);
      return 0;
    }
    if (len<size) {
      buf[len]=0;
      return buf;
    }
  }
}

char *xreadfile(char *name, char *buf, off_t len)
{
  if (!(buf = readfile(name, buf, len))) perror_exit("Bad '%s'", name);

  return buf;
}

// The data argument to ioctl() is actually long, but it's usually used as
// a pointer. If you need to feed in a number, do (void *)(long) typecast.
int xioctl(int fd, int request, void *data)
{
  int rc;

  errno = 0;
  rc = ioctl(fd, request, data);
  if (rc == -1 && errno) perror_exit("ioctl %x", request);

  return rc;
}

// Open a /var/run/NAME.pid file, dying if we can't write it or if it currently
// exists and is this executable.
void xpidfile(char *name)
{
  char pidfile[256], spid[32];
  int i, fd;
  pid_t pid;

  sprintf(pidfile, "/var/run/%s.pid", name);
  // Try three times to open the sucker.
  for (i=0; i<3; i++) {
    fd = open(pidfile, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if (fd != -1) break;

    // If it already existed, read it.  Loop for race condition.
    fd = open(pidfile, O_RDONLY);
    if (fd == -1) continue;

    // Is the old program still there?
    spid[xread(fd, spid, sizeof(spid)-1)] = 0;
    close(fd);
    pid = atoi(spid);
    if (pid < 1 || (kill(pid, 0) && errno == ESRCH)) unlink(pidfile);

    // An else with more sanity checking might be nice here.
  }

  if (i == 3) error_exit("xpidfile %s", name);

  xwrite(fd, spid, sprintf(spid, "%ld\n", (long)getpid()));
  close(fd);
}

// Copy the rest of in to out and close both files.

long long xsendfile(int in, int out)
{
  long long total = 0;
  long len;

  if (in<0) return 0;
  for (;;) {
    len = xread(in, libbuf, sizeof(libbuf));
    if (len<1) break;
    xwrite(out, libbuf, len);
    total += len;
  }

  return total;
}

// parse fractional seconds with optional s/m/h/d suffix
long xparsetime(char *arg, long units, long *fraction)
{
  double d;
  long l;

  if (CFG_TOYBOX_FLOAT) d = strtod(arg, &arg);
  else l = strtoul(arg, &arg, 10);

  // Parse suffix
  if (*arg) {
    int ismhd[]={1,60,3600,86400}, i = stridx("smhd", *arg);

    if (i == -1) error_exit("Unknown suffix '%c'", *arg);
    if (CFG_TOYBOX_FLOAT) d *= ismhd[i];
    else l *= ismhd[i];
  }

  if (CFG_TOYBOX_FLOAT) {
    l = (long)d;
    if (fraction) *fraction = units*(d-l);
  } else if (fraction) *fraction = 0;

  return l;
}

// Compile a regular expression into a regex_t
void xregcomp(regex_t *preg, char *regex, int cflags)
{
  int rc = regcomp(preg, regex, cflags);

  if (rc) {
    regerror(rc, preg, libbuf, sizeof(libbuf));
    error_exit("xregcomp: %s", libbuf);
  }
}

char *xtzset(char *new)
{
  char *old = getenv("TZ");

  if (old) old = xstrdup(old);
  if (new ? setenv("TZ", new, 1) : unsetenv("TZ")) perror_exit("setenv");
  tzset();

  return old;
}

// Set a signal handler
void xsignal(int signal, void *handler)
{
  struct sigaction *sa = (void *)libbuf;

  memset(sa, 0, sizeof(struct sigaction));
  sa->sa_handler = handler;

  if (sigaction(signal, sa, 0)) perror_exit("xsignal %d", signal);
}
