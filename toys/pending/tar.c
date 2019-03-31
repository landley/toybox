/* tar.c - create/extract archives
 *
 * Copyright 2014 Ashwini Kumar <ak.ashwini81@gmail.com>
 *
 * For the command, see
 *   http://pubs.opengroup.org/onlinepubs/007908799/xcu/tar.html
 * For the modern file format, see
 *   http://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html#tag_20_92_13_06
 *   https://en.wikipedia.org/wiki/Tar_(computing)#File_format
 *   https://www.gnu.org/software/tar/manual/html_node/Tar-Internals.html
 *
 * For writing to external program
 * http://www.gnu.org/software/tar/manual/html_node/Writing-to-an-External-Program.html
 *
 * Toybox will never implement the "pax" command as a matter of policy.
 *
 * Why --exclude pattern but no --include? tar cvzf a.tgz dir --include '*.txt'
 * Extract into dir same as filename, --restrict? "Tarball is splodey"
 *

USE_TAR(NEWTOY(tar, "&(no-recursion)(numeric-owner)(no-same-permissions)(overwrite)(exclude)*(mtime):(group):(owner):(to-command):o(no-same-owner)p(same-permissions)k(keep-old)c(create)|h(dereference)x(extract)|t(list)|v(verbose)j(bzip2)z(gzip)O(to-stdout)m(touch)X(exclude-from)*T(files-from)*C(directory):f(file):[!txc][!jz]", TOYFLAG_USR|TOYFLAG_BIN))

config TAR
  bool "tar"
  default n
  help
    usage: tar [-cxtjzhmvO] [-X FILE] [-T FILE] [-f TARFILE] [-C DIR]

    Create, extract, or list files in a .tar (or compressed t?z) file. 

    Options:
    c  Create                x  Extract               t  Test
    f  Name of TARFILE       C  Change to DIR first   v  Verbose: show filenames
    o  Ignore owner          h  Follow symlinks       m  Ignore mtime
    j  bzip2 compression     z  gzip compression
    O  Extract to stdout     X  exclude names in FILE T  include names in FILE
    --exclude=FILE File pattern(s) to exclude
*/

#define FOR_tar
#include "toys.h"

GLOBALS(
  char *f, *C;
  struct arg_list *T, *X;
  char *to_command, *owner, *group, *mtime;
  struct arg_list *exclude;

  struct double_list *incl, *excl, *seen;
  struct string_list *dirs;
  char *cwd;
  int fd, ouid, ggid, hlc, warn;
  time_t mtt;

  // hardlinks seen so far (hlc many)
  struct {
    char *arg;
    ino_t ino;
    dev_t dev;
  } *hlx;

  // Parsed information about a tar header.
  struct {
    char *name, *link_target, *uname, *gname;
    long long size;
    uid_t uid;
    gid_t gid;
    mode_t mode;
    time_t mtime;
    dev_t device;
  } hdr;
)

struct tar_hdr {
  char name[100], mode[8], uid[8], gid[8],size[12], mtime[12], chksum[8],
       type, link[100], magic[8], uname[32], gname[32], major[8], minor[8],
       prefix[155], padd[12];
};

// convert to int to octal (or base-256)
static void itoo(char *str, int len, unsigned long long val)
{
  // Do we need binary encoding?
  if (!(val>>(3*(len-1)))) sprintf(str, "%0*llo", len-1, val);
  else {
    *str = 128;
    while (--len) *++str = val>>(3*len);
  }
}
#define ITOO(x, y) itoo(x, sizeof(x), y)

// convert octal (or base-256) to int
static unsigned long long otoi(char *str, unsigned len)
{
  unsigned long long val = 0;

  // When tar value too big or octal, use binary encoding with high bit set
  if (128&*str) while (--len) val = (val<<8)+*++str;
  else {
    while (len && *str>='0' && *str<='7') val = val*8+*str++-'0', len--;
    if (len && *str && *str != ' ') error_exit("bad header");
  }

  return val;
}

// Calculate packet checksum, with cksum field treated as 8 spaces
static unsigned cksum(void *data)
{
  unsigned i, cksum = 8*' ';

  for (i = 0; i<500; i += (i==147) ? 9 : 1) cksum += ((char *)data)[i];

  return cksum;
}

static void write_longname(char *name, char type)
{
  struct tar_hdr tmp;
  int sz = strlen(name) +1;

  memset(&tmp, 0, sizeof(tmp));
  strcpy(tmp.name, "././@LongLink");
  ITOO(tmp.mode, 0);
  ITOO(tmp.uid, 0);
  ITOO(tmp.gid, 0);
  ITOO(tmp.size, sz);
  ITOO(tmp.mtime, 0);
  tmp.type = type;
  strcpy(tmp.magic, "ustar  ");

  // Calculate checksum. Since 0777777 is twice 512*255 it can never use more
  // than 6 digits, last byte is ' ' or historical reasons.
  itoo(tmp.chksum, sizeof(tmp.chksum)-1, cksum(&tmp));
  tmp.chksum[7] = ' ';

  // write header and name, padded with NUL to block size
  xwrite(TT.fd, &tmp, 512);
  xwrite(TT.fd, name, sz);
  if (sz%512) xwrite(TT.fd, toybuf, 512-(sz%512));
}

static struct double_list *filter(struct double_list *lst, char *name)
{
  struct double_list *end = lst;

  if (lst)
    do if (!fnmatch(lst->data, name, 1<<3)) return lst;
    while (end != (lst = lst->next));

  return 0;
}

static void skippy(long long len)
{
  if (lskip(TT.fd, len)) perror_exit("EOF");
}

// allocate and read data from TT.fd
static void alloread(void *buf, int len)
{
  // actually void **, but automatic typecasting doesn't work with void ** :(
  void **b = buf;

  free(*b);
  *b = xmalloc(len+1);
  xreadall(TT.fd, *b, len);
  b[len] = 0;
}

static void add_file(char **nam, struct stat *st)
{
  struct tar_hdr hdr;
  struct passwd *pw = pw;
  struct group *gr = gr;
  int i, fd =-1;
  char *c, *p, *name = *nam, *lnk, *hname;

  // exclusion defaults to --no-anchored and --wildcards-match-slash
  for (p = name; *p; p++)
    if ((p == name || p[-1] == '/') && *p != '/' && filter(TT.excl, p)) return;

  if (S_ISDIR(st->st_mode) && name[strlen(name)-1] != '/') {
    lnk = xmprintf("%s/", name);
    free(name);
    *nam = name = lnk;
  }

  // remove leading / and any .. entries from saved name
  for (hname = name; *hname == '/'; hname++);
  for (c = hname;;) {
    if (!(c = strstr(c, ".."))) break;
    if (c == hname || c[-1] == '/') {
      if (!c[2]) return;
      if (c[2]=='/') c = hname = c+3;
    } else c+= 2;
  }
  if (!*hname) return;

  if (TT.warn && hname != name) {
    fprintf(stderr, "removing leading '%.*s' from member names\n",
           (int)(hname-name), name);
    TT.warn = 0;
  }

  if (TT.owner) st->st_uid = TT.ouid;
  if (TT.group) st->st_gid = TT.ggid;
  if (TT.mtime) st->st_mtime = TT.mtt;

  memset(&hdr, 0, sizeof(hdr));
  strncpy(hdr.name, hname, sizeof(hdr.name));
  ITOO(hdr.mode, st->st_mode &07777);
  ITOO(hdr.uid, st->st_uid);
  ITOO(hdr.gid, st->st_gid);
  ITOO(hdr.size, 0); //set size later
  ITOO(hdr.mtime, st->st_mtime);

  // Hard link or symlink? i=0 neither, i=1 hardlink, i=2 symlink

  // Are there hardlinks to a non-directory entry?
  if (st->st_nlink>1 && !S_ISDIR(st->st_mode)) {
    // Have we seen this dev&ino before?
    for (i = 0; i<TT.hlc; i++) {
      if (st->st_ino == TT.hlx[i].ino && st->st_dev == TT.hlx[i].dev)
        break;
    }
    if (i != TT.hlc) {
      lnk = TT.hlx[i].arg;
      i = 1;
    } else {
      // first time we've seen it, store as normal file.
      if (!(TT.hlc&255)) TT.hlx = xrealloc(TT.hlx, TT.hlc+256);
      TT.hlx[TT.hlc].arg = xstrdup(hname);
      TT.hlx[TT.hlc].ino = st->st_ino;
      TT.hlx[TT.hlc].dev = st->st_dev;
      TT.hlc++;
      i = 0;
    }
  } else i = 0;

  // !i because hardlink to a symlink is a thing.
  if (!i && S_ISLNK(st->st_mode)) {
    i = 2;
    lnk = xreadlink(name);
  }

  // Handle file types
  if (i) {
    hdr.type = '0'+i;
    if (i==2 && !(lnk = xreadlink(name))) return perror_msg("readlink");
    if (strlen(lnk) > sizeof(hdr.link)) write_longname(lnk, 'K');
    strncpy(hdr.link, lnk, sizeof(hdr.link));
    if (i) free(lnk);
  } else if (S_ISREG(st->st_mode)) {
    hdr.type = '0';
    ITOO(hdr.size, st->st_size);
  } else if (S_ISDIR(st->st_mode)) hdr.type = '5';
  else if (S_ISFIFO(st->st_mode)) hdr.type = '6';
  else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
    hdr.type = (S_ISCHR(st->st_mode))?'3':'4';
    ITOO(hdr.major, dev_major(st->st_rdev));
    ITOO(hdr.minor, dev_minor(st->st_rdev));
  } else return error_msg("unknown file type '%o'", st->st_mode & S_IFMT);

  if (strlen(hname) > sizeof(hdr.name)) write_longname(hname, 'L');
  strcpy(hdr.magic, "ustar  ");
  if (!FLAG(numeric_owner)) {
    if (TT.owner || (pw = bufgetpwuid(st->st_uid)))
      strncpy(hdr.uname, TT.owner ? TT.owner : pw->pw_name, sizeof(hdr.uname));
    if (TT.group || (gr = bufgetgrgid(st->st_gid)))
      strncpy(hdr.gname, TT.group ? TT.group : gr->gr_name, sizeof(hdr.gname));
  }

  itoo(hdr.chksum, sizeof(hdr.chksum)-1, cksum(&hdr));
  hdr.chksum[7] = ' ';

  if (FLAG(v)) printf("%s\n", hname);

  // Write header and data to archive
  xwrite(TT.fd, &hdr, 512);
  if (hdr.type != '0') return; //nothing to write
  if ((fd = open(name, O_RDONLY)) < 0)
    return perror_msg("can't open '%s'", name);
  xsendfile_pad(fd, TT.fd, st->st_size);
  if (st->st_size%512) writeall(TT.fd, toybuf, (512-(st->st_size%512)));
  close(fd);
}

static int add_to_tar(struct dirtree *node)
{
  struct stat st;
  char *path;

  if (!dirtree_notdotdot(node)) return 0;
  if (!fstat(TT.fd, &st) && st.st_dev == node->st.st_dev
      && st.st_ino == node->st.st_ino) {
    error_msg("'%s' file is the archive; not dumped", TT.f);
    return 0;
  }

  path = dirtree_path(node, 0);
  add_file(&path, &(node->st)); //path may be modified
  free(path);

  return (DIRTREE_RECURSE|(FLAG(h)?DIRTREE_SYMFOLLOW:0))*!FLAG(no_recursion);
}

// Does anybody actually use this?
static void extract_to_command(void)
{
  int pipefd[2], status = 0;
  pid_t cpid;

  if (!S_ISREG(TT.hdr.mode)) return; //only regular files are supported.

  xpipe(pipefd);
  if (!(cpid = xfork())) {    // Child reads from pipe
    char buf[64], *argv[4] = {"sh", "-c", TT.to_command, NULL};

    setenv("TAR_FILETYPE", "f", 1);
    sprintf(buf, "%0o", TT.hdr.mode);
    setenv("TAR_MODE", buf, 1);
    sprintf(buf, "%ld", (long)TT.hdr.size);
    setenv("TAR_SIZE", buf, 1);
    setenv("TAR_FILENAME", TT.hdr.name, 1);
    setenv("TAR_UNAME", TT.hdr.uname, 1);
    setenv("TAR_GNAME", TT.hdr.gname, 1);
    sprintf(buf, "%0llo", (long long)TT.hdr.mtime);
    setenv("TAR_MTIME", buf, 1);
    sprintf(buf, "%0o", TT.hdr.uid);
    setenv("TAR_UID", buf, 1);
    sprintf(buf, "%0o", TT.hdr.gid);
    setenv("TAR_GID", buf, 1);

    xclose(pipefd[1]); // Close unused write
    dup2(pipefd[0], 0);
    signal(SIGPIPE, SIG_DFL);
    xexec(argv);
  } else {
    xclose(pipefd[0]);  // Close unused read end
    xsendfile_len(TT.fd, pipefd[1], TT.hdr.size);
    xclose(pipefd[1]);
    waitpid(cpid, &status, 0);
    if (WIFSIGNALED(status))
      xprintf("tar : %d: child returned %d\n", cpid, WTERMSIG(status));
  }
}

// Do pending directory utimes(), NULL to flush all.
static int dirflush(char *name)
{
  char *s = s, *ss;

  // Barf if name not in TT.cwd
  if (name) {
    ss = s = xabspath(name, -1);
    if (TT.cwd[1] && (!strstart(&ss, TT.cwd) || *ss!='/')) {
      error_msg("'%s' not under '%s'", name, TT.cwd);
      free(s);

      return 1;
    }
  }

  // Set deferred utimes() for directories this file isn't under.
  // (Files must be depth-first ordered in tarball for this to matter.)
  while (TT.dirs) {
    long long ll = *(long long *)TT.dirs->str;
    struct timeval times[2] = {{ll, 0},{ll, 0}};

    // If next file is under (or equal to) this dir, keep waiting
    if (name && strstart(&ss, ss = s) && (!*ss || *ss=='/')) break;

    if (utimes(TT.dirs->str+sizeof(long long), times))
      perror_msg("utimes %lld %s", ll,
        TT.dirs->str+sizeof(long long));
    free(llist_pop(&TT.dirs));
  }
  free(s);

  // name was under TT.cwd
  return 0;
}

static void extract_to_disk(void)
{
  char *name = TT.hdr.name;
  int ala = TT.hdr.mode;

  if (dirflush(name)) {
    if (S_ISREG(ala) && !TT.hdr.link_target) skippy(TT.hdr.size);
 
    return;
  }

  // create path before file if necessary
  if (strrchr(name, '/') && mkpath(name) && errno !=EEXIST)
      return perror_msg(":%s: can't mkdir", name);

  // remove old file, if exists
  if (!FLAG(k) && !S_ISDIR(ala) && unlink(name) && errno!=ENOENT)
    return perror_msg("can't remove: %s", name);

  if (S_ISREG(ala)) {
    // hardlink?
    if (TT.hdr.link_target) {
      if (link(TT.hdr.link_target, name))
        return perror_msg("can't link '%s' -> '%s'", name, TT.hdr.link_target);
    // write contents
    } else {
      int fd = xcreate(name, O_WRONLY|O_CREAT|(FLAG(overwrite)?O_TRUNC:O_EXCL),
        WARN_ONLY|(ala & 07777));
      if (fd != -1) {
        xsendfile_len(TT.fd, fd, TT.hdr.size);
        close(fd);
      }
    }
  } else if (S_ISDIR(ala)) {
    if ((mkdir(name, 0700) == -1) && errno != EEXIST)
      return perror_msg("%s: can't create", TT.hdr.name);
  } else if (S_ISLNK(ala)) {
    if (symlink(TT.hdr.link_target, TT.hdr.name))
      return perror_msg("can't link '%s' -> '%s'", name, TT.hdr.link_target);
  } else if (mknod(name, ala, TT.hdr.device))
    return perror_msg("can't create '%s'", name);


  if (!FLAG(o) && !geteuid()) {
    //set ownership..., --no-same-owner, --numeric-owner
    int u = TT.hdr.uid, g = TT.hdr.gid;

    if (TT.owner) TT.hdr.uid = TT.ouid;
    else if (!FLAG(numeric_owner)) {
      struct passwd *pw = getpwnam(TT.hdr.uname);
      if (pw && (TT.owner || !FLAG(numeric_owner))) TT.hdr.uid = pw->pw_uid;
    }

    if (TT.group) TT.hdr.gid = TT.ggid;
    else if (!FLAG(numeric_owner)) {
      struct group *gr = getgrnam(TT.hdr.gname);
      if (gr) TT.hdr.gid = gr->gr_gid;
    }

    if (lchown(name, u, g)) perror_msg("chown %d:%d '%s'", u, g, name);;
  }

  // || !FLAG(no_same_permissions))
  if (FLAG(p) && !S_ISLNK(ala)) chmod(TT.hdr.name, ala);

  // Apply mtime.
  if (!FLAG(m)) {
    if (S_ISDIR(ala)) {
      struct string_list *sl;

      // Writing files into a directory changes directory timestamps, so
      // defer mtime updates until contents written.

      sl = xmalloc(sizeof(struct string_list)+sizeof(long long)+strlen(name)+1);
      *(long long *)sl->str = TT.hdr.mtime;
      strcpy(sl->str+sizeof(long long), name);
      sl->next = TT.dirs;
      TT.dirs = sl;
    } else {
      struct timeval times[2] = {{TT.hdr.mtime, 0},{TT.hdr.mtime, 0}};
      utimes(TT.hdr.name, times);
    }
  }
}

static void unpack_tar(void)
{
  struct double_list *walk, *delete;
  struct tar_hdr tar;
  int i, and = 0;
  unsigned maj, min;
  char *s;

  for (;;) {
    // align to next block and read it
    if (TT.hdr.size%512) skippy(512-TT.hdr.size%512);
    i = readall(TT.fd, &tar, 512);

    if (i && i != 512) error_exit("read error");

    // Two consecutive empty headers ends tar even if there's more data
    if (!i || !*tar.name) {
      if (!i || and++) {
        dirflush(0);
        return;
      }
      TT.hdr.size = 0;
      continue;
    }
    // ensure null temination even of pathological packets
    tar.padd[0] = and = 0;

    // Is this a valid Unix Standard TAR header?
    if (memcmp(tar.magic, "ustar", 5)) error_exit("bad header");
    if (cksum(&tar) != otoi(tar.chksum, sizeof(tar.chksum)))
      error_exit("bad cksum");
    TT.hdr.size = otoi(tar.size, sizeof(tar.size));

    // If this header isn't writing something to the filesystem
    if (tar.type<'0' || tar.type>'7') {

      // Long name extension header?
      if (tar.type == 'K') alloread(&TT.hdr.link_target, TT.hdr.size);
      else if (tar.type == 'L') alloread(&TT.hdr.name, TT.hdr.size);
      else if (tar.type == 'x') {
        char *p, *buf = 0;
        int i, len, n;

        // Posix extended record "LEN NAME=VALUE\n" format
        alloread(&buf, TT.hdr.size);
        for (p = buf; (p-buf)<TT.hdr.size; p += len) {
          i = sscanf(p, "%u path=%n", &len, &n);
          if (i<1 || len<4 || len>TT.hdr.size) {
            error_msg("bad header");
            break;
          }
          p[len-1] = 0;
          if (i == 2) {
            TT.hdr.name = xstrdup(p+n);
            break;
          }
        }
        free(buf);

      // Ignore everything else.
      } else skippy(TT.hdr.size);

      continue;
    }

    // At this point, we have something to output. Convert metadata.
    TT.hdr.mode = otoi(tar.mode, sizeof(tar.mode));
    TT.hdr.mode |= (char []){8,8,10,2,6,4,1,8}[tar.type-'0']<<12;
    TT.hdr.uid = otoi(tar.uid, sizeof(tar.uid));
    TT.hdr.gid = otoi(tar.gid, sizeof(tar.gid));
    TT.hdr.mtime = otoi(tar.mtime, sizeof(tar.mtime));
    maj = otoi(tar.major, sizeof(tar.major));
    min = otoi(tar.minor, sizeof(tar.minor));
    TT.hdr.device = dev_makedev(maj, min);

    TT.hdr.uname = xstrndup(TT.owner ? TT.owner : tar.uname, sizeof(tar.uname));
    TT.hdr.gname = xstrndup(TT.group ? TT.group : tar.gname, sizeof(tar.gname));

    if (TT.owner) TT.hdr.uid = TT.ouid;
    else if (!FLAG(numeric_owner)) {
      struct passwd *pw = getpwnam(TT.hdr.uname);
      if (pw && (TT.owner || !FLAG(numeric_owner))) TT.hdr.uid = pw->pw_uid;
    }

    if (TT.group) TT.hdr.gid = TT.ggid;
    else if (!FLAG(numeric_owner)) {
      struct group *gr = getgrnam(TT.hdr.gname);
      if (gr) TT.hdr.gid = gr->gr_gid;
    }

    if (!TT.hdr.link_target && *tar.link)
      TT.hdr.link_target = xstrndup(tar.link, sizeof(tar.link));
    if (!TT.hdr.name) {
      // Glue prefix and name fields together with / if necessary
      i = strnlen(tar.prefix, sizeof(tar.prefix));
      TT.hdr.name = xmprintf("%.*s%s%.*s", i, tar.prefix,
        (i && tar.prefix[i-1] != '/') ? "/" : "",
        (int)sizeof(tar.name), tar.name);
    }

    // Old broken tar recorded dir as "file with trailing slash"
    if (S_ISREG(TT.hdr.mode) && (s = strend(TT.hdr.name, "/"))) {
      *s = 0;
      TT.hdr.mode = (TT.hdr.mode & ~S_IFMT) | S_IFDIR;
    }

    // Non-regular files don't have contents stored in archive.
    if ((TT.hdr.link_target && *TT.hdr.link_target) || !S_ISREG(TT.hdr.mode))
      TT.hdr.size = 0;

    // Files are seen even if excluded, so check them here.
    // TT.seen points to first seen entry in TT.incl, or NULL if none yet.
    if ((delete = filter(TT.incl, TT.hdr.name)) && TT.incl != TT.seen) {
      if (!TT.seen) TT.seen = delete;

      // Move seen entry to end of list.
      if (TT.incl == delete) TT.incl = TT.incl->next;
      else for (walk = TT.incl; walk != TT.seen; walk = walk->next) {
        if (walk == delete) {
          dlist_pop(&walk);
          dlist_add_nomalloc(&TT.incl, delete);
        }
      }
    }

    // Skip excluded files
    if (filter(TT.excl, TT.hdr.name) || (TT.incl && !delete))
      skippy(TT.hdr.size);
    else if (FLAG(t)) {
      if (FLAG(v)) {
        struct tm *lc = localtime(TT.mtime ? &TT.mtt : &TT.hdr.mtime);
        char perm[11];

        mode_to_string(TT.hdr.mode, perm);
        printf("%s %s/%s ", perm, TT.hdr.uname, TT.hdr.gname);
        if (tar.type=='3' || tar.type=='4') printf("%u,%u", maj, min);
        else printf("%9lld", (long long)TT.hdr.size);
        printf("  %d-%02d-%02d %02d:%02d:%02d ", 1900+lc->tm_year, 1+lc->tm_mon,
          lc->tm_mday, lc->tm_hour, lc->tm_min, lc->tm_sec);
      }
      printf("%s", TT.hdr.name);
      if (TT.hdr.link_target) printf(" -> %s", TT.hdr.link_target);
      xputc('\n');
      skippy(TT.hdr.size);
    } else {
      if (FLAG(v)) printf("%s\n", TT.hdr.name);
      if (FLAG(O)) xsendfile_len(TT.fd, 0, TT.hdr.size);
      else if (FLAG(to_command)) extract_to_command();
      else extract_to_disk();
    }

    free(TT.hdr.name);
    free(TT.hdr.link_target);
    free(TT.hdr.uname);
    free(TT.hdr.gname);
    TT.hdr.name = TT.hdr.link_target = 0;
  }
}

// Add copy of filename to TT.incl or TT.excl, minus trailing \n and /
static void trim_list(char **pline, long len)
{
  char *n = strdup(*pline);
  int i = strlen(n);

  dlist_add(TT.X ? &TT.excl : &TT.incl, n);
  if (i && n[i-1]=='\n') i--;
  while (i && n[i-1] == '/') i--;
  n[i] = 0;
}

void tar_main(void)
{
  char *s, **args = toys.optargs;

  // When extracting to command
  signal(SIGPIPE, SIG_IGN);

  if (!geteuid()) toys.optflags |= FLAG_p;
  if (TT.owner) TT.ouid = xgetuid(TT.owner);
  if (TT.group) TT.ggid = xgetgid(TT.group);
  if (TT.mtime) xparsedate(TT.mtime, &TT.mtt, (void *)&s, 1); 

  // Collect file list. Note: trim_list appends to TT.incl when !TT.X
  for (;TT.X; TT.X = TT.X->next) do_lines(xopenro(TT.X->arg), '\n', trim_list);
  for (args = toys.optargs; *args; args++) trim_list(args, strlen(*args));
  for (;TT.T; TT.T = TT.T->next) do_lines(xopenro(TT.T->arg), '\n', trim_list);

  // Open archive file
  if (FLAG(c)) {
    if (!TT.incl) error_exit("empty archive");
    TT.fd = 1;
  }
  if (TT.f && strcmp(TT.f, "-"))
    TT.fd = xcreate(TT.f, TT.fd*(O_WRONLY|O_CREAT|O_TRUNC), 0666);

  // Get destination directory
  if (TT.C) xchdir(TT.C);
  TT.cwd = xabspath(s = xgetcwd(), 1);
  free(s);

  // Are we reading?
  if (FLAG(x)||FLAG(t)) {
    if (FLAG(j)||FLAG(z)) {
      int pipefd[2] = {TT.fd, -1};

      xpopen_both((char *[]){FLAG(z)?"gunzip":"bunzip2", "-cf", "-", NULL},
        pipefd);
      close(TT.fd);
      TT.fd = pipefd[1];
    }

    unpack_tar();
    if (TT.seen != TT.incl) {
      if (!TT.seen) TT.seen = TT.incl;
      while (TT.incl != TT.seen) {
        error_msg("'%s' not in archive", TT.incl->data);
        TT.incl = TT.incl->next;
      }
    }

  // are we writing? (Don't have to test flag here, one of 3 must be set)
  } else {
    struct double_list *dl = TT.incl;

    if (FLAG(j)||FLAG(z)) {
      int pipefd[2] = {-1, TT.fd};

      xpopen_both((char *[]){FLAG(z)?"gzip":"bzip2", "-f", NULL}, pipefd);
      close(TT.fd);
      TT.fd = pipefd[0];
    }
    do {
      TT.warn = 1;
      dirtree_flagread(dl->data, FLAG(h)?DIRTREE_SYMFOLLOW:0, add_to_tar);
    } while (TT.incl != (dl = dl->next));

    writeall(TT.fd, toybuf, 1024);
  }

  if (CFG_TOYBOX_FREE) {
    while(TT.hlc) free(TT.hlx[--TT.hlc].arg);
    free(TT.hlx);
    close(TT.fd);
  }
}
