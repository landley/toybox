/* lib.c - various reusable stuff.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

void verror_msg(char *msg, int err, va_list va)
{
  char *s = ": %s";

  fprintf(stderr, "%s: ", toys.which->name);
  if (msg) vfprintf(stderr, msg, va);
  else s+=2;
  if (err) fprintf(stderr, s, strerror(err));
  if (msg || err) putc('\n', stderr);
  if (!toys.exitval) toys.exitval++;
}

// These functions don't collapse together because of the va_stuff.

void error_msg(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, 0, va);
  va_end(va);
}

void perror_msg(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, errno, va);
  va_end(va);
}

// Die with an error message.
void error_exit(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, 0, va);
  va_end(va);

  xexit();
}

// Die with an error message and strerror(errno)
void perror_exit(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, errno, va);
  va_end(va);

  xexit();
}

// Exit with an error message after showing help text.
void help_exit(char *msg, ...)
{
  va_list va;

  if (CFG_TOYBOX_HELP)
    fprintf(stderr, "See %s --help\n", toys.which->name);

  if (msg) {
    va_start(va, msg);
    verror_msg(msg, 0, va);
    va_end(va);
  }

  xexit();
}

// If you want to explicitly disable the printf() behavior (because you're
// printing user-supplied data, or because android's static checker produces
// false positives for 'char *s = x ? "blah1" : "blah2"; printf(s);' and it's
// -Werror there for policy reasons).
void error_msg_raw(char *msg)
{
  error_msg("%s", msg);
}

void perror_msg_raw(char *msg)
{
  perror_msg("%s", msg);
}

void error_exit_raw(char *msg)
{
  error_exit("%s", msg);
}

void perror_exit_raw(char *msg)
{
  perror_exit("%s", msg);
}

// Keep reading until full or EOF
ssize_t readall(int fd, void *buf, size_t len)
{
  size_t count = 0;

  while (count<len) {
    int i = read(fd, (char *)buf+count, len-count);
    if (!i) break;
    if (i<0) return i;
    count += i;
  }

  return count;
}

// Keep writing until done or EOF
ssize_t writeall(int fd, void *buf, size_t len)
{
  size_t count = 0;
  while (count<len) {
    int i = write(fd, count+(char *)buf, len-count);
    if (i<1) return i;
    count += i;
  }

  return count;
}

// skip this many bytes of input. Return 0 for success, >0 means this much
// left after input skipped.
off_t lskip(int fd, off_t offset)
{
  off_t cur = lseek(fd, 0, SEEK_CUR);

  if (cur != -1) {
    off_t end = lseek(fd, 0, SEEK_END) - cur;

    if (end > 0 && end < offset) return offset - end;
    end = offset+cur;
    if (end == lseek(fd, end, SEEK_SET)) return 0;
    perror_exit("lseek");
  }

  while (offset>0) {
    int try = offset>sizeof(libbuf) ? sizeof(libbuf) : offset, or;

    or = readall(fd, libbuf, try);
    if (or < 0) perror_exit("lskip to %lld", (long long)offset);
    else offset -= or;
    if (or < try) break;
  }

  return offset;
}

// flags: 1=make last dir (with mode lastmode, otherwise skips last component)
//        2=make path (already exists is ok)
//        4=verbose
// returns 0 = path ok, 1 = error
int mkpathat(int atfd, char *dir, mode_t lastmode, int flags)
{
  struct stat buf;
  char *s;

  // mkdir -p one/two/three is not an error if the path already exists,
  // but is if "three" is a file. The others we dereference and catch
  // not-a-directory along the way, but the last one we must explicitly
  // test for. Might as well do it up front.

  if (!fstatat(atfd, dir, &buf, 0) && !S_ISDIR(buf.st_mode)) {
    errno = EEXIST;
    return 1;
  }

  for (s = dir; ;s++) {
    char save = 0;
    mode_t mode = (0777&~toys.old_umask)|0300;

    // find next '/', but don't try to mkdir "" at start of absolute path
    if (*s == '/' && (flags&2) && s != dir) {
      save = *s;
      *s = 0;
    } else if (*s) continue;

    // Use the mode from the -m option only for the last directory.
    if (!save) {
      if (flags&1) mode = lastmode;
      else break;
    }

    if (mkdirat(atfd, dir, mode)) {
      if (!(flags&2) || errno != EEXIST) return 1;
    } else if (flags&4)
      fprintf(stderr, "%s: created directory '%s'\n", toys.which->name, dir);

    if (!(*s = save)) break;
  }

  return 0;
}

// Split a path into linked list of components, tracking head and tail of list.
// Filters out // entries with no contents.
struct string_list **splitpath(char *path, struct string_list **list)
{
  char *new = path;

  *list = 0;
  do {
    int len;

    if (*path && *path != '/') continue;
    len = path-new;
    if (len > 0) {
      *list = xmalloc(sizeof(struct string_list) + len + 1);
      (*list)->next = 0;
      memcpy((*list)->str, new, len);
      (*list)->str[len] = 0;
      list = &(*list)->next;
    }
    new = path+1;
  } while (*path++);

  return list;
}

// Find all file in a colon-separated path with access type "type" (generally
// X_OK or R_OK).  Returns a list of absolute paths to each file found, in
// order.

struct string_list *find_in_path(char *path, char *filename)
{
  struct string_list *rlist = NULL, **prlist=&rlist;
  char *cwd;

  if (!path) return 0;

  cwd = xgetcwd();
  for (;;) {
    char *next = strchr(path, ':');
    int len = next ? next-path : strlen(path);
    struct string_list *rnext;
    struct stat st;

    rnext = xmalloc(sizeof(void *) + strlen(filename)
      + (len ? len : strlen(cwd)) + 2);
    if (!len) sprintf(rnext->str, "%s/%s", cwd, filename);
    else {
      char *res = rnext->str;

      memcpy(res, path, len);
      res += len;
      *(res++) = '/';
      strcpy(res, filename);
    }

    // Confirm it's not a directory.
    if (!stat(rnext->str, &st) && S_ISREG(st.st_mode)) {
      *prlist = rnext;
      rnext->next = NULL;
      prlist = &(rnext->next);
    } else free(rnext);

    if (!next) break;
    path += len;
    path++;
  }
  free(cwd);

  return rlist;
}

long long estrtol(char *str, char **end, int base)
{
  errno = 0;

  return strtoll(str, end, base);
}

long long xstrtol(char *str, char **end, int base)
{
  long long l = estrtol(str, end, base);

  if (errno) perror_exit_raw(str);

  return l;
}

// atol() with the kilo/mega/giga/tera/peta/exa extensions, plus word and block.
// (zetta and yotta don't fit in 64 bits.)
long long atolx(char *numstr)
{
  char *c = numstr, *suffixes="cwbkmgtpe", *end;
  long long val;

  val = xstrtol(numstr, &c, 0);
  if (c != numstr && *c && (end = strchr(suffixes, tolower(*c)))) {
    int shift = end-suffixes-2;

    if (shift==-1) val *= 2;
    if (!shift) val *= 512;
    else if (shift>0) {
      if (toupper(*++c)=='d') while (shift--) val *= 1000;
      else val *= 1LL<<(shift*10);
    }
  }
  while (isspace(*c)) c++;
  if (c==numstr || *c) error_exit("not integer: %s", numstr);

  return val;
}

long long atolx_range(char *numstr, long long low, long long high)
{
  long long val = atolx(numstr);

  if (val < low) error_exit("%lld < %lld", val, low);
  if (val > high) error_exit("%lld > %lld", val, high);

  return val;
}

int stridx(char *haystack, char needle)
{
  char *off;

  if (!needle) return -1;
  off = strchr(haystack, needle);
  if (!off) return -1;

  return off-haystack;
}

char *strlower(char *s)
{
  char *try, *new;

  if (!CFG_TOYBOX_I18N) {
    try = new = xstrdup(s);
    for (; *s; s++) *(new++) = tolower(*s);
  } else {
    // I can't guarantee the string _won't_ expand during reencoding, so...?
    try = new = xmalloc(strlen(s)*2+1);

    while (*s) {
      wchar_t c;
      int len = mbrtowc(&c, s, MB_CUR_MAX, 0);

      if (len < 1) *(new++) = *(s++);
      else {
        s += len;
        // squash title case too
        c = towlower(c);

        // if we had a valid utf8 sequence, convert it to lower case, and can't
        // encode back to utf8, something is wrong with your libc. But just
        // in case somebody finds an exploit...
        len = wcrtomb(new, c, 0);
        if (len < 1) error_exit("bad utf8 %x", (int)c);
        new += len;
      }
    }
    *new = 0;
  }

  return try;
}

// strstr but returns pointer after match
char *strafter(char *haystack, char *needle)
{
  char *s = strstr(haystack, needle);

  return s ? s+strlen(needle) : s;
}

// Remove trailing \n
char *chomp(char *s)
{
  char *p = strrchr(s, '\n');

  if (p && !p[1]) *p = 0;
  return s;
}

int unescape(char c)
{
  char *from = "\\abefnrtv", *to = "\\\a\b\033\f\n\r\t\v";
  int idx = stridx(from, c);

  return (idx == -1) ? 0 : to[idx];
}

// If string ends with suffix return pointer to start of suffix in string,
// else NULL
char *strend(char *str, char *suffix)
{
  long a = strlen(str), b = strlen(suffix);

  if (a>b && !strcmp(str += a-b, suffix)) return str;

  return 0;
}

// If *a starts with b, advance *a past it and return 1, else return 0;
int strstart(char **a, char *b)
{
  int len = strlen(b), i = !strncmp(*a, b, len);

  if (i) *a += len;

  return i;
}

// Return how long the file at fd is, if there's any way to determine it.
off_t fdlength(int fd)
{
  struct stat st;
  off_t base = 0, range = 1, expand = 1, old;

  if (!fstat(fd, &st) && S_ISREG(st.st_mode)) return st.st_size;

  // If the ioctl works for this, return it.
  // TODO: is blocksize still always 512, or do we stat for it?
  // unsigned int size;
  // if (ioctl(fd, BLKGETSIZE, &size) >= 0) return size*512L;

  // If not, do a binary search for the last location we can read.  (Some
  // block devices don't do BLKGETSIZE right.)  This should probably have
  // a CONFIG option...

  // If not, do a binary search for the last location we can read.

  old = lseek(fd, 0, SEEK_CUR);
  do {
    char temp;
    off_t pos = base + range / 2;

    if (lseek(fd, pos, 0)>=0 && read(fd, &temp, 1)==1) {
      off_t delta = (pos + 1) - base;

      base += delta;
      if (expand) range = (expand <<= 1) - base;
      else range -= delta;
    } else {
      expand = 0;
      range = pos - base;
    }
  } while (range > 0);

  lseek(fd, old, SEEK_SET);

  return base;
}

// Read contents of file as a single nul-terminated string.
// measure file size if !len, allocate buffer if !buf
// Existing buffers need len in *plen
// Returns amount of data read in *plen
char *readfileat(int dirfd, char *name, char *ibuf, off_t *plen)
{
  off_t len, rlen;
  int fd;
  char *buf, *rbuf;

  // Unsafe to probe for size with a supplied buffer, don't ever do that.
  if (CFG_TOYBOX_DEBUG && (ibuf ? !*plen : *plen)) error_exit("bad readfileat");

  if (-1 == (fd = openat(dirfd, name, O_RDONLY))) return 0;

  // If we dunno the length, probe it. If we can't probe, start with 1 page.
  if (!*plen) {
    if ((len = fdlength(fd))>0) *plen = len;
    else len = 4096;
  } else len = *plen-1;

  if (!ibuf) buf = xmalloc(len+1);
  else buf = ibuf;

  for (rbuf = buf;;) {
    rlen = readall(fd, rbuf, len);
    if (*plen || rlen<len) break;

    // If reading unknown size, expand buffer by 1.5 each time we fill it up.
    rlen += rbuf-buf;
    buf = xrealloc(buf, len = (rlen*3)/2);
    rbuf = buf+rlen;
    len -= rlen;
  }
  *plen = len = rlen+(rbuf-buf);
  close(fd);

  if (rlen<0) {
    if (ibuf != buf) free(buf);
    buf = 0;
  } else buf[len] = 0;

  return buf;
}

char *readfile(char *name, char *ibuf, off_t len)
{
  return readfileat(AT_FDCWD, name, ibuf, &len);
}

// Sleep for this many thousandths of a second
void msleep(long miliseconds)
{
  struct timespec ts;

  ts.tv_sec = miliseconds/1000;
  ts.tv_nsec = (miliseconds%1000)*1000000;
  nanosleep(&ts, &ts);
}

// return 1<<x of highest bit set
int highest_bit(unsigned long l)
{
  int i;

  for (i = 0; l; i++) l >>= 1;

  return i-1;
}

// Inefficient, but deals with unaligned access
int64_t peek_le(void *ptr, unsigned size)
{
  int64_t ret = 0;
  char *c = ptr;
  int i;

  for (i=0; i<size; i++) ret |= ((int64_t)c[i])<<(i*8);
  return ret;
}

int64_t peek_be(void *ptr, unsigned size)
{
  int64_t ret = 0;
  char *c = ptr;
  int i;

  for (i=0; i<size; i++) ret = (ret<<8)|(c[i]&0xff);
  return ret;
}

int64_t peek(void *ptr, unsigned size)
{
  return IS_BIG_ENDIAN ? peek_be(ptr, size) : peek_le(ptr, size);
}

void poke(void *ptr, uint64_t val, int size)
{
  if (size & 8) {
    volatile uint64_t *p = (uint64_t *)ptr;
    *p = val;
  } else if (size & 4) {
    volatile int *p = (int *)ptr;
    *p = val;
  } else if (size & 2) {
    volatile short *p = (short *)ptr;
    *p = val;
  } else {
    volatile char *p = (char *)ptr;
    *p = val;
  }
}

// Iterate through an array of files, opening each one and calling a function
// on that filehandle and name. The special filename "-" means stdin if
// flags is O_RDONLY, stdout otherwise. An empty argument list calls
// function() on just stdin/stdout.
//
// Note: pass O_CLOEXEC to automatically close filehandles when function()
// returns, otherwise filehandles must be closed by function().
// pass WARN_ONLY to produce warning messages about files it couldn't
// open/create, and skip them. Otherwise function is called with fd -1.
void loopfiles_rw(char **argv, int flags, int permissions,
  void (*function)(int fd, char *name))
{
  int fd, failok = !(flags&WARN_ONLY);

  flags &= ~WARN_ONLY;

  // If no arguments, read from stdin.
  if (!*argv) function((flags & O_ACCMODE) != O_RDONLY ? 1 : 0, "-");
  else do {
    // Filename "-" means read from stdin.
    // Inability to open a file prints a warning, but doesn't exit.

    if (!strcmp(*argv, "-")) fd = 0;
    else if (0>(fd = notstdio(open(*argv, flags, permissions))) && !failok) {
      perror_msg_raw(*argv);
      continue;
    }
    function(fd, *argv);
    if ((flags & O_CLOEXEC) && fd) close(fd);
  } while (*++argv);
}

// Call loopfiles_rw with O_RDONLY|O_CLOEXEC|WARN_ONLY (common case)
void loopfiles(char **argv, void (*function)(int fd, char *name))
{
  loopfiles_rw(argv, O_RDONLY|O_CLOEXEC|WARN_ONLY, 0, function);
}

// Slow, but small.

char *get_rawline(int fd, long *plen, char end)
{
  char c, *buf = NULL;
  long len = 0;

  for (;;) {
    if (1>read(fd, &c, 1)) break;
    if (!(len & 63)) buf=xrealloc(buf, len+65);
    if ((buf[len++]=c) == end) break;
  }
  if (buf) buf[len]=0;
  if (plen) *plen = len;

  return buf;
}

char *get_line(int fd)
{
  long len;
  char *buf = get_rawline(fd, &len, '\n');

  if (buf && buf[--len]=='\n') buf[len]=0;

  return buf;
}

int wfchmodat(int fd, char *name, mode_t mode)
{
  int rc = fchmodat(fd, name, mode, 0);

  if (rc) {
    perror_msg("chmod '%s' to %04o", name, mode);
    toys.exitval=1;
  }
  return rc;
}

static char *tempfile2zap;
static void tempfile_handler(void)
{
  if (1 < (long)tempfile2zap) unlink(tempfile2zap);
}

// Open a temporary file to copy an existing file into.
int copy_tempfile(int fdin, char *name, char **tempname)
{
  struct stat statbuf;
  int fd;
  int ignored __attribute__((__unused__));

  *tempname = xmprintf("%s%s", name, "XXXXXX");
  if(-1 == (fd = mkstemp(*tempname))) error_exit("no temp file");
  if (!tempfile2zap) sigatexit(tempfile_handler);
  tempfile2zap = *tempname;

  // Set permissions of output file (ignoring errors, usually due to nonroot)

  fstat(fdin, &statbuf);
  fchmod(fd, statbuf.st_mode);

  // We chmod before chown, which strips the suid bit. Caller has to explicitly
  // switch it back on if they want to keep suid.

  // Suppress warn-unused-result. Both gcc and clang clutch their pearls about
  // this but it's _supposed_ to fail when we're not root.
  ignored = fchown(fd, statbuf.st_uid, statbuf.st_gid);

  return fd;
}

// Abort the copy and delete the temporary file.
void delete_tempfile(int fdin, int fdout, char **tempname)
{
  close(fdin);
  close(fdout);
  if (*tempname) unlink(*tempname);
  tempfile2zap = (char *)1;
  free(*tempname);
  *tempname = NULL;
}

// Copy the rest of the data and replace the original with the copy.
void replace_tempfile(int fdin, int fdout, char **tempname)
{
  char *temp = xstrdup(*tempname);

  temp[strlen(temp)-6]=0;
  if (fdin != -1) {
    xsendfile(fdin, fdout);
    xclose(fdin);
  }
  xclose(fdout);
  rename(*tempname, temp);
  tempfile2zap = (char *)1;
  free(*tempname);
  free(temp);
  *tempname = NULL;
}

// Create a 256 entry CRC32 lookup table.

void crc_init(unsigned int *crc_table, int little_endian)
{
  unsigned int i;

  // Init the CRC32 table (big endian)
  for (i=0; i<256; i++) {
    unsigned int j, c = little_endian ? i : i<<24;
    for (j=8; j; j--)
      if (little_endian) c = (c&1) ? (c>>1)^0xEDB88320 : c>>1;
      else c=c&0x80000000 ? (c<<1)^0x04c11db7 : (c<<1);
    crc_table[i] = c;
  }
}

// Init base64 table

void base64_init(char *p)
{
  int i;

  for (i = 'A'; i != ':'; i++) {
    if (i == 'Z'+1) i = 'a';
    if (i == 'z'+1) i = '0';
    *(p++) = i;
  }
  *(p++) = '+';
  *(p++) = '/';
}

int yesno(int def)
{
  char buf;

  fprintf(stderr, " (%c/%c):", def ? 'Y' : 'y', def ? 'n' : 'N');
  fflush(stderr);
  while (fread(&buf, 1, 1, stdin)) {
    int new;

    // The letter changes the value, the newline (or space) returns it.
    if (isspace(buf)) break;
    if (-1 != (new = stridx("ny", tolower(buf)))) def = new;
  }

  return def;
}

struct signame {
  int num;
  char *name;
};

// Signals required by POSIX 2008:
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html

#define SIGNIFY(x) {SIG##x, #x}

static struct signame signames[] = {
  SIGNIFY(ABRT), SIGNIFY(ALRM), SIGNIFY(BUS),
  SIGNIFY(FPE), SIGNIFY(HUP), SIGNIFY(ILL), SIGNIFY(INT), SIGNIFY(KILL),
  SIGNIFY(PIPE), SIGNIFY(QUIT), SIGNIFY(SEGV), SIGNIFY(TERM),
  SIGNIFY(USR1), SIGNIFY(USR2), SIGNIFY(SYS), SIGNIFY(TRAP),
  SIGNIFY(VTALRM), SIGNIFY(XCPU), SIGNIFY(XFSZ),

  // Start of non-terminal signals

  SIGNIFY(CHLD), SIGNIFY(CONT), SIGNIFY(STOP), SIGNIFY(TSTP),
  SIGNIFY(TTIN), SIGNIFY(TTOU), SIGNIFY(URG)
};

// not in posix: SIGNIFY(STKFLT), SIGNIFY(WINCH), SIGNIFY(IO), SIGNIFY(PWR)
// obsolete: SIGNIFY(PROF) SIGNIFY(POLL)

// Handler that sets toys.signal, and writes to toys.signalfd if set
void generic_signal(int sig)
{
  if (toys.signalfd) {
    char c = sig;

    writeall(toys.signalfd, &c, 1);
  }
  toys.signal = sig;
}

void exit_signal(int sig)
{
  if (sig) toys.exitval = sig|128;
  xexit();
}

// Install the same handler on every signal that defaults to killing the
// process, calling the handler on the way out. Calling multiple times
// adds the handlers to a list, to be called in order.
void sigatexit(void *handler)
{
  struct arg_list *al = xmalloc(sizeof(struct arg_list));
  int i;

  for (i=0; signames[i].num != SIGCHLD; i++)
    signal(signames[i].num, exit_signal);
  al->next = toys.xexit;
  al->arg = handler;
  toys.xexit = al;
}

// Convert name to signal number.  If name == NULL print names.
int sig_to_num(char *pidstr)
{
  int i;

  if (pidstr) {
    char *s;

    i = estrtol(pidstr, &s, 10);
    if (!errno && !*s) return i;

    if (!strncasecmp(pidstr, "sig", 3)) pidstr+=3;
  }
  for (i = 0; i < sizeof(signames)/sizeof(struct signame); i++)
    if (!pidstr) xputs(signames[i].name);
    else if (!strcasecmp(pidstr, signames[i].name)) return signames[i].num;

  return -1;
}

char *num_to_sig(int sig)
{
  int i;

  for (i=0; i<sizeof(signames)/sizeof(struct signame); i++)
    if (signames[i].num == sig) return signames[i].name;
  return NULL;
}

// premute mode bits based on posix mode strings.
mode_t string_to_mode(char *modestr, mode_t mode)
{
  char *whos = "ogua", *hows = "=+-", *whats = "xwrstX", *whys = "ogu",
       *s, *str = modestr;
  mode_t extrabits = mode & ~(07777);

  // Handle octal mode
  if (isdigit(*str)) {
    mode = estrtol(str, &s, 8);
    if (errno || *s || (mode & ~(07777))) goto barf;

    return mode | extrabits;
  }

  // Gaze into the bin of permission...
  for (;;) {
    int i, j, dowho, dohow, dowhat, amask;

    dowho = dohow = dowhat = amask = 0;

    // Find the who, how, and what stanzas, in that order
    while (*str && (s = strchr(whos, *str))) {
      dowho |= 1<<(s-whos);
      str++;
    }
    // If who isn't specified, like "a" but honoring umask.
    if (!dowho) {
      dowho = 8;
      umask(amask=umask(0));
    }
    if (!*str || !(s = strchr(hows, *str))) goto barf;
    dohow = *(str++);

    if (!dohow) goto barf;
    while (*str && (s = strchr(whats, *str))) {
      dowhat |= 1<<(s-whats);
      str++;
    }

    // Convert X to x for directory or if already executable somewhere
    if ((dowhat&32) &&  (S_ISDIR(mode) || (mode&0111))) dowhat |= 1;

    // Copy mode from another category?
    if (!dowhat && *str && (s = strchr(whys, *str))) {
      dowhat = (mode>>(3*(s-whys)))&7;
      str++;
    }

    // Are we ready to do a thing yet?
    if (*str && *(str++) != ',') goto barf;

    // Ok, apply the bits to the mode.
    for (i=0; i<4; i++) {
      for (j=0; j<3; j++) {
        mode_t bit = 0;
        int where = 1<<((3*i)+j);

        if (amask & where) continue;

        // Figure out new value at this location
        if (i == 3) {
          // suid/sticky bit.
          if (j) {
            if ((dowhat & 8) && (dowho&(8|(1<<i)))) bit++;
          } else if (dowhat & 16) bit++;
        } else {
          if (!(dowho&(8|(1<<i)))) continue;
          if (dowhat&(1<<j)) bit++;
        }

        // When selection active, modify bit

        if (dohow == '=' || (bit && dohow == '-')) mode &= ~where;
        if (bit && dohow != '-') mode |= where;
      }
    }

    if (!*str) break;
  }

  return mode|extrabits;
barf:
  error_exit("bad mode '%s'", modestr);
}

// Format access mode into a drwxrwxrwx string
void mode_to_string(mode_t mode, char *buf)
{
  char c, d;
  int i, bit;

  buf[10]=0;
  for (i=0; i<9; i++) {
    bit = mode & (1<<i);
    c = i%3;
    if (!c && (mode & (1<<((d=i/3)+9)))) {
      c = "tss"[d];
      if (!bit) c &= ~0x20;
    } else c = bit ? "xwr"[c] : '-';
    buf[9-i] = c;
  }

  if (S_ISDIR(mode)) c = 'd';
  else if (S_ISBLK(mode)) c = 'b';
  else if (S_ISCHR(mode)) c = 'c';
  else if (S_ISLNK(mode)) c = 'l';
  else if (S_ISFIFO(mode)) c = 'p';
  else if (S_ISSOCK(mode)) c = 's';
  else c = '-';
  *buf = c;
}

// basename() can modify its argument or return a pointer to a constant string
// This just gives after the last '/' or the whole stirng if no /
char *getbasename(char *name)
{
  char *s = strrchr(name, '/');

  if (s) return s+1;

  return name;
}

// Execute a callback for each PID that matches a process name from a list.
void names_to_pid(char **names, int (*callback)(pid_t pid, char *name))
{
  DIR *dp;
  struct dirent *entry;

  if (!(dp = opendir("/proc"))) perror_exit("opendir");

  while ((entry = readdir(dp))) {
    unsigned u;
    char *cmd, **curname;

    if (!(u = atoi(entry->d_name))) continue;
    sprintf(libbuf, "/proc/%u/cmdline", u);
    if (!(cmd = readfile(libbuf, libbuf, sizeof(libbuf)))) continue;

    for (curname = names; *curname; curname++)
      if (**curname == '/' ? !strcmp(cmd, *curname)
          : !strcmp(getbasename(cmd), getbasename(*curname)))
        if (callback(u, *curname)) break;
    if (*curname) break;
  }
  closedir(dp);
}

// display first few digits of number with power of two units
int human_readable(char *buf, unsigned long long num, int style)
{
  unsigned long long snap = 0;
  int len, unit, divisor = (style&HR_1000) ? 1000 : 1024;

  // Divide rounding up until we have 3 or fewer digits. Since the part we
  // print is decimal, the test is 999 even when we divide by 1024.
  // We can't run out of units because 2<<64 is 18 exabytes.
  // test 5675 is 5.5k not 5.6k.
  for (unit = 0; num > 999; unit++) num = ((snap = num)+(divisor/2))/divisor;
  len = sprintf(buf, "%llu", num);
  if (unit && len == 1) {
    // Redo rounding for 1.2M case, this works with and without HR_1000.
    num = snap/divisor;
    snap -= num*divisor;
    snap = ((snap*100)+50)/divisor;
    snap /= 10;
    len = sprintf(buf, "%llu.%llu", num, snap);
  }
  if (style & HR_SPACE) buf[len++] = ' ';
  if (unit) {
    unit = " kMGTPE"[unit];

    if (!(style&HR_1000)) unit = toupper(unit);
    buf[len++] = unit;
  } else if (style & HR_B) buf[len++] = 'B';
  buf[len] = 0;

  return len;
}

// The qsort man page says you can use alphasort, the posix committee
// disagreed, and doubled down: http://austingroupbugs.net/view.php?id=142
// So just do our own. (The const is entirely to humor the stupid compiler.)
int qstrcmp(const void *a, const void *b)
{
  return strcmp(*(char **)a, *(char **)b);
}

// According to http://www.opengroup.org/onlinepubs/9629399/apdxa.htm
// we should generate a uuid structure by reading a clock with 100 nanosecond
// precision, normalizing it to the start of the gregorian calendar in 1582,
// and looking up our eth0 mac address.
//
// On the other hand, we have 128 bits to come up with a unique identifier, of
// which 6 have a defined value.  /dev/urandom it is.

void create_uuid(char *uuid)
{
  // Read 128 random bits
  int fd = xopenro("/dev/urandom");
  xreadall(fd, uuid, 16);
  close(fd);

  // Claim to be a DCE format UUID.
  uuid[6] = (uuid[6] & 0x0F) | 0x40;
  uuid[8] = (uuid[8] & 0x3F) | 0x80;

  // rfc2518 section 6.4.1 suggests if we're not using a macaddr, we should
  // set bit 1 of the node ID, which is the mac multicast bit.  This means we
  // should never collide with anybody actually using a macaddr.
  uuid[11] |= 128;
}

char *show_uuid(char *uuid)
{
  char *out = libbuf;
  int i;

  for (i=0; i<16; i++) out+=sprintf(out, "-%02x"+!(0x550&(1<<i)), uuid[i]);
  *out = 0;

  return libbuf;
}

// Returns pointer to letter at end, 0 if none. *start = initial %
char *next_printf(char *s, char **start)
{
  for (; *s; s++) {
    if (*s != '%') continue;
    if (*++s == '%') continue;
    if (start) *start = s-1;
    while (0 <= stridx("0'#-+ ", *s)) s++;
    while (isdigit(*s)) s++;
    if (*s == '.') s++;
    while (isdigit(*s)) s++;

    return s;
  }

  return 0;
}

// Posix inexplicably hasn't got this, so find str in line.
char *strnstr(char *line, char *str)
{
  long len = strlen(str);
  char *s;

  for (s = line; *s; s++) if (!strncasecmp(s, str, len)) break;

  return *s ? s : 0;
}

int dev_minor(int dev)
{
  return ((dev&0xfff00000)>>12)|(dev&0xff);
}

int dev_major(int dev)
{
  return (dev&0xfff00)>>8;
}

int dev_makedev(int major, int minor)
{
  return (minor&0xff)|((major&0xfff)<<8)|((minor&0xfff00)<<12);
}

// Return cached passwd entries.
struct passwd *bufgetpwuid(uid_t uid)
{
  struct pwuidbuf_list {
    struct pwuidbuf_list *next;
    struct passwd pw;
  } *list;
  struct passwd *temp;
  static struct pwuidbuf_list *pwuidbuf;

  for (list = pwuidbuf; list; list = list->next)
    if (list->pw.pw_uid == uid) return &(list->pw);

  list = xmalloc(512);
  list->next = pwuidbuf;

  errno = getpwuid_r(uid, &list->pw, sizeof(*list)+(char *)list,
    512-sizeof(*list), &temp);
  if (!temp) {
    free(list);

    return 0;
  }
  pwuidbuf = list;

  return &list->pw;
}

// Return cached passwd entries.
struct group *bufgetgrgid(gid_t gid)
{
  struct grgidbuf_list {
    struct grgidbuf_list *next;
    struct group gr;
  } *list;
  struct group *temp;
  static struct grgidbuf_list *grgidbuf;

  for (list = grgidbuf; list; list = list->next)
    if (list->gr.gr_gid == gid) return &(list->gr);

  list = xmalloc(512);
  list->next = grgidbuf;

  errno = getgrgid_r(gid, &list->gr, sizeof(*list)+(char *)list,
    512-sizeof(*list), &temp);
  if (!temp) {
    free(list);

    return 0;
  }
  grgidbuf = list;

  return &list->gr;
}

// Always null terminates, returns 0 for failure, len for success
int readlinkat0(int dirfd, char *path, char *buf, int len)
{
  if (!len) return 0;

  len = readlinkat(dirfd, path, buf, len-1);
  if (len<1) return 0;
  buf[len] = 0;

  return len;
}

int readlink0(char *path, char *buf, int len)
{
  return readlinkat0(AT_FDCWD, path, buf, len);
}

// Do regex matching handling embedded NUL bytes in string (hence extra len
// argument). Note that neither the pattern nor the match can currently include
// NUL bytes (even with wildcards) and string must be null terminated at
// string[len]. But this can find a match after the first NUL.
int regexec0(regex_t *preg, char *string, long len, int nmatch,
  regmatch_t pmatch[], int eflags)
{
  char *s = string;

  for (;;) {
    long ll = 0;
    int rc;

    while (len && !*s) {
      s++;
      len--;
    }
    while (s[ll] && ll<len) ll++;

    rc = regexec(preg, s, nmatch, pmatch, eflags);
    if (!rc) {
      for (rc = 0; rc<nmatch && pmatch[rc].rm_so!=-1; rc++) {
        pmatch[rc].rm_so += s-string;
        pmatch[rc].rm_eo += s-string;
      }

      return 0;
    }
    if (ll==len) return rc;

    s += ll;
    len -= ll;
  }
}

// Return user name or string representation of number, returned buffer
// lasts until next call.
char *getusername(uid_t uid)
{
  struct passwd *pw = bufgetpwuid(uid);
  static char unum[12];

  sprintf(unum, "%u", (unsigned)uid);
  return pw ? pw->pw_name : unum;
}

// Return group name or string representation of number, returned buffer
// lasts until next call.
char *getgroupname(gid_t gid)
{
  struct group *gr = bufgetgrgid(gid);
  static char gnum[12];

  sprintf(gnum, "%u", (unsigned)gid);
  return gr ? gr->gr_name : gnum;
}

// Iterate over lines in file, calling function. Function can write 0 to
// the line pointer if they want to keep it, or 1 to terminate processing,
// otherwise line is freed. Passed file descriptor is closed at the end.
void do_lines(int fd, void (*call)(char **pline, long len))
{
  FILE *fp = fd ? xfdopen(fd, "r") : stdin;

  for (;;) {
    char *line = 0;
    ssize_t len;

    len = getline(&line, (void *)&len, fp);
    if (len > 0) {
      call(&line, len);
      if (line == (void *)1) break;
      free(line);
    } else break;
  }

  if (fd) fclose(fp);
}
