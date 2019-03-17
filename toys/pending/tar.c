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

USE_TAR(NEWTOY(tar, "&(no-recursion)(numeric-owner)(no-same-permissions)(overwrite)(exclude)*(to-command):o(no-same-owner)p(same-permissions)k(keep-old)c(create)|h(dereference)x(extract)|t(list)|v(verbose)j(bzip2)z(gzip)O(to-stdout)m(touch)X(exclude-from)*T(files-from)*C(directory):f(file):[!txc][!jz]", TOYFLAG_USR|TOYFLAG_BIN))

todo: support .txz
todo: directory timestamps not set on extract
todo: extract into chmod 000 directory

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
    j  Force bzip2 format    z  Force gzip format
    O  Extract to stdout
    X  File of names to exclude
    T  File of names to include
    --exclude=FILE File pattern(s) to exclude
*/

#define FOR_tar
#include "toys.h"

GLOBALS(
  char *f, *C;
  struct arg_list *T, *X;
  char *to_command;
  struct arg_list *exc;

// exc is an argument but inc isn't?
  struct arg_list *inc, *pass;
  void *inodes;
  char *cwd;
  int fd;

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

struct inode_list {
  struct inode_list *next;
  char *arg;
  ino_t ino;
  dev_t dev;
};

//convert to octal
static void itoo(char *str, int len, off_t val)
{
  sprintf(str, "%0*llo", len-1, (unsigned long long)val);
}

// This really needs a hash table
static struct inode_list *seen_inode(void **list, struct stat *st, char *name)
{
  if (!S_ISDIR(st->st_mode) && st->st_nlink > 1) {
    struct inode_list *new;

    for (new = *list; new; new = new->next)
      if(new->ino == st->st_ino && new->dev == st->st_dev)
        return new;

    new = xzalloc(sizeof(*new));
    new->ino = st->st_ino;
    new->dev = st->st_dev;
    new->arg = xstrdup(name);
    new->next = *list;
    *list = new;
  }
  return 0;
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
  struct tar_hdr tmp[2];
  int sz = strlen(name) +1;

  memset(tmp, 0, sizeof(tmp));
  strcpy(tmp->name, "././@LongLink");
  memset(tmp->mode, '0', sizeof(tmp->mode)-1);
  memset(tmp->uid, '0', sizeof(tmp->uid)-1);
  memset(tmp->gid, '0', sizeof(tmp->gid)-1);
  itoo(tmp->size, sizeof(tmp->size), sz);
  memset(tmp->mtime, '0', sizeof(tmp->mtime)-1);
  tmp->type = type;
  strcpy(tmp->magic, "ustar  ");

  // Calculate checksum
  itoo(tmp->chksum, sizeof(tmp->chksum), cksum(&tmp));

  // write header and name, padded with NUL to block size
  xwrite(TT.fd, tmp, sizeof(*tmp));
  xwrite(TT.fd, name, sz);
  xwrite(TT.fd, tmp+1, 512-(sz%512));
}

static int filter(struct arg_list *lst, char *name)
{
  for (; lst; lst = lst->next) if (!fnmatch(lst->arg, name, 1<<3)) return 1;

  return 0;
}

static void skippy(long long len)
{
  if (lskip(TT.fd, len)) perror_exit("EOF");
}

// actually void **, but automatic typecasting doesn't work with void ** :(
static void alloread(void *buf, int len)
{
  void **b = buf;

  free(*b);
  *b = xmalloc(len+1);
  xreadall(TT.fd, *b, len);
  b[len] = 0;
}

static void add_file(char **nam, struct stat *st)
{
  struct tar_hdr hdr;
  struct passwd *pw;
  struct group *gr;
  struct inode_list *node = node;
  int i, fd =-1;
  char *c, *p, *name = *nam, *lnk, *hname, buf[512] = {0,};
  static int warn = 1;

  for (p = name; *p; p++)
    if ((p == name || p[-1] == '/') && *p != '/' && filter(TT.exc, p)) return;

  if (S_ISDIR(st->st_mode) && name[strlen(name)-1] != '/') {
    lnk = xmprintf("%s/",name);
    free(name);
    *nam = name = lnk;
  }
  hname = name;
  //remove leading '/' or relative path '../' component
  if (*hname == '/') hname++;
  if (!*hname) return;
  while ((c = strstr(hname, "../"))) hname = c + 3;
  if (warn && hname != name) {
    fprintf(stderr, "removing leading '%.*s' from member names\n",
           (int)(hname-name), name);
    warn = 0;
  }

  memset(&hdr, 0, sizeof(hdr));
  strncpy(hdr.name, hname, sizeof(hdr.name));
  itoo(hdr.mode, sizeof(hdr.mode), st->st_mode &07777);
  itoo(hdr.uid, sizeof(hdr.uid), st->st_uid);
  itoo(hdr.gid, sizeof(hdr.gid), st->st_gid);
  itoo(hdr.size, sizeof(hdr.size), 0); //set size later
  itoo(hdr.mtime, sizeof(hdr.mtime), st->st_mtime);

  // Hard link or symlink?
  i = !!S_ISLNK(st->st_mode);
  if (i || (node = seen_inode(&TT.inodes, st, hname))) {
// TODO: test preserve symlink ownership
    hdr.type = '1'+i;
    if (!(lnk = i ? xreadlink(name) : node->arg)) return perror_msg("readlink");
// TODO: does this need NUL terminator?
    if (strlen(lnk) > sizeof(hdr.link))
      write_longname(lnk, 'K'); //write longname LINK
// TODO: this will error_exit() if too long, not truncate.
    xstrncpy(hdr.link, lnk, sizeof(hdr.link));
    if (i) free(lnk);
  } else if (S_ISREG(st->st_mode)) {
    hdr.type = '0';
    if (st->st_size <= (off_t)077777777777LL)
      itoo(hdr.size, sizeof(hdr.size), st->st_size);
    else {
// TODO: test accept 12 7's but don't emit without terminator
      return error_msg("TODO: need base-256 encoding for '%s' '%lld'\n",
                hname, (unsigned long long)st->st_size);
    }
  } else if (S_ISDIR(st->st_mode)) hdr.type = '5';
  else if (S_ISFIFO(st->st_mode)) hdr.type = '6';
  else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
    hdr.type = (S_ISCHR(st->st_mode))?'3':'4';
    itoo(hdr.major, sizeof(hdr.major), dev_major(st->st_rdev));
    itoo(hdr.minor, sizeof(hdr.minor), dev_minor(st->st_rdev));
  } else return error_msg("unknown file type '%o'", st->st_mode & S_IFMT);

  if (strlen(hname) > sizeof(hdr.name))
          write_longname(hname, 'L'); //write longname NAME
  strcpy(hdr.magic, "ustar  ");
  if ((pw = bufgetpwuid(st->st_uid)))
    snprintf(hdr.uname, sizeof(hdr.uname), "%s", pw->pw_name);
  else sprintf(hdr.uname, "%d", st->st_uid);

  if ((gr = bufgetgrgid(st->st_gid)))
    snprintf(hdr.gname, sizeof(hdr.gname), "%s", gr->gr_name);
  else sprintf(hdr.gname, "%d", st->st_gid);

  itoo(hdr.chksum, sizeof(hdr.chksum)-1, cksum(&hdr));
  if (FLAG(v)) printf("%s\n",hname);
  xwrite(TT.fd, (void*)&hdr, 512);

  //write actual data to archive
  if (hdr.type != '0') return; //nothing to write
  if ((fd = open(name, O_RDONLY)) < 0) {
    perror_msg("can't open '%s'", name);
    return;
  }
  xsendfile_pad(fd, TT.fd, st->st_size);
  if (st->st_size%512) writeall(TT.fd, buf, (512-(st->st_size%512)));
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
  if (FLAG(no_recursion)) return 0;
  return ((DIRTREE_RECURSE | (FLAG(h)?DIRTREE_SYMFOLLOW:0)));
}

// Does anybody actually use this?
static void extract_to_command(void)
{
  int pipefd[2], status = 0;
  pid_t cpid;

  xpipe(pipefd);
  if (!S_ISREG(TT.hdr.mode)) return; //only regular files are supported.

  cpid = fork();
  if (cpid == -1) perror_exit("fork");

  if (!cpid) {    // Child reads from pipe
    char buf[64], *argv[4] = {"sh", "-c", TT.to_command, NULL};

    setenv("TAR_FILETYPE", "f", 1);
    sprintf(buf, "%0o", TT.hdr.mode);
    setenv("TAR_MODE", buf, 1);
    sprintf(buf, "%ld", (long)TT.hdr.size);
    setenv("TAR_SIZE", buf, 1);
    setenv("TAR_FILENAME", TT.hdr.name, 1);
    setenv("TAR_UNAME", TT.hdr.uname, 1);
    setenv("TAR_GNAME", TT.hdr.gname, 1);
    sprintf(buf, "%0o", (int)TT.hdr.mtime);
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

static void extract_to_disk(void)
{
  int flags, dst_fd = -1;
  char *s;
  struct stat ex;

// while not if
  flags = strlen(TT.hdr.name);
  if (flags>2)
    if (strstr(TT.hdr.name, "/../") || !strcmp(TT.hdr.name, "../") ||
        !strcmp(TT.hdr.name+flags-3, "/.."))
      error_msg("drop %s", TT.hdr.name);

  if (TT.hdr.name[flags-1] == '/') TT.hdr.name[flags-1] = 0;
  //Regular file with preceding path
  if ((s = strrchr(TT.hdr.name, '/')) && mkpath(TT.hdr.name) && errno !=EEXIST)
      return error_msg(":%s: not created", TT.hdr.name);

  //remove old file, if exists
  if (!FLAG(k) && !S_ISDIR(TT.hdr.mode) && !lstat(TT.hdr.name, &ex))
    if (unlink(TT.hdr.name)) perror_msg("can't remove: %s", TT.hdr.name);

  //hard link
  if (S_ISREG(TT.hdr.mode) && TT.hdr.link_target) {
    if (link(TT.hdr.link_target, TT.hdr.name))
      perror_msg("can't link '%s' -> '%s'", TT.hdr.name, TT.hdr.link_target);
    goto COPY;
  }

  switch (TT.hdr.mode & S_IFMT) {
    case S_IFREG:
      flags = O_WRONLY|O_CREAT|O_EXCL;
      if (FLAG(overwrite)) flags = O_WRONLY|O_CREAT|O_TRUNC;
      dst_fd = open(TT.hdr.name, flags, TT.hdr.mode & 07777);
      if (dst_fd == -1) perror_msg("%s: can't open", TT.hdr.name);
      break;
    case S_IFDIR:
      if ((mkdir(TT.hdr.name, TT.hdr.mode) == -1) && errno != EEXIST)
        perror_msg("%s: can't create", TT.hdr.name);
      break;
    case S_IFLNK:
      if (symlink(TT.hdr.link_target, TT.hdr.name))
        perror_msg("can't link '%s' -> '%s'", TT.hdr.name, TT.hdr.link_target);
      break;
    case S_IFBLK:
    case S_IFCHR:
    case S_IFIFO:
      if (mknod(TT.hdr.name, TT.hdr.mode, TT.hdr.device))
        perror_msg("can't create '%s'", TT.hdr.name);
      break;
    default:
      printf("type %o not supported\n", TT.hdr.mode);
      break;
  }

  //copy file....
COPY:
  xsendfile_len(TT.fd, dst_fd, TT.hdr.size);
  close(dst_fd);

  if (!FLAG(o)) {
    //set ownership..., --no-same-owner, --numeric-owner
    uid_t u = TT.hdr.uid;
    gid_t g = TT.hdr.gid;

    if (!FLAG(numeric_owner)) {
      struct group *gr = getgrnam(TT.hdr.gname);
      struct passwd *pw = getpwnam(TT.hdr.uname);
      if (pw) u = pw->pw_uid;
      if (gr) g = gr->gr_gid;
    }
    if (!geteuid() && lchown(TT.hdr.name, u, g))
      perror_msg("chown %d:%d '%s'", u, g, TT.hdr.name);;
  }

  // || !FLAG(no_same_permissions))
  if (FLAG(p) && !S_ISLNK(TT.hdr.mode)) chmod(TT.hdr.name, TT.hdr.mode);

// TODO: defer directory mtime until we've finished with contents
  //apply mtime
  if (!FLAG(m)) {
    struct timeval times[2] = {{TT.hdr.mtime, 0},{TT.hdr.mtime, 0}};
    utimes(TT.hdr.name, times);
  }
}

static void add_to_list(struct arg_list **llist, char *name)
{
  struct arg_list **list = llist;

  while (*list) list=&((*list)->next);
  *list = xzalloc(sizeof(struct arg_list));
  (*list)->arg = name;
  if ((name[strlen(name)-1] == '/') && strlen(name) != 1)
    name[strlen(name)-1] = '\0';
}

static void file_to_list(char *file, struct arg_list **llist)
{
  int fd = xopenro(file);
  char *line = 0;

  while ((line = get_line(fd))) add_to_list(llist, xstrdup(line));
  if (fd) close(fd);
  free(line);
}

//convert octal to int
static unsigned long long otoi(char *str, int len)
{
  unsigned long long val;

// todo: base-256 encoding, just do it symmetrically for all fields
  str[len-1] = 0;
  val = strtoull(str, &str, 8);
  if (*str && *str != ' ') error_exit("bad header");

  return val;
}

static void unpack_tar(void)
{
  struct tar_hdr tar;
  int i;
  char *s;

  for (;;) {
    // align to next block and read it
    if (TT.hdr.size%512) skippy(512-TT.hdr.size%512);

    i = readall(TT.fd, &tar, 512);
    if (i != 512) error_exit("read error");
    // ensure null temination even of pathological packets
    tar.padd[0] = 0;
    // End of tar
    if (!*tar.name) return;

// can you append a bzip to a gzip _within_ a tarball? Nested compress?
// Or compressed data after uncompressed data?
    if (memcmp(tar.magic, "ustar", 5)) error_exit("bad header");
    if (cksum(&tar) != otoi(tar.chksum, sizeof(tar.chksum)))
      error_exit("bad cksum");
    TT.hdr.size = otoi(tar.size, sizeof(tar.size));

    // If this header isn't writing something to the filesystem
    if (tar.type<'0' || tar.type>'7') {
      if (tar.type == 'K') alloread(&TT.hdr.link_target, TT.hdr.size);
      else if (tar.type == 'L') alloread(&TT.hdr.name, TT.hdr.size);
      else if (tar.type == 'x') {
        char *p, *buf = 0;
        int i, len, n;

        // Posix extended record "LEN NAME=VALUE\n" format
        alloread(&buf, TT.hdr.size);
        for (p = buf; (p-buf)<TT.hdr.size; p += len) {
          if ((i = sscanf(p, "%u path=%n", &len, &n))<1 || len<4 ||
              len>TT.hdr.size)
          {
            error_msg("corrupted extended header");
            break;
          }
          p[len-1] = 0;
          if (i == 2) {
            TT.hdr.name = xstrdup(p+n);
            break;
          }
        }
        free(buf);

      // This could be if (strchr("DMNSVg", tar.type)) but an unknown header
      // type with trailing contents is unlikely to have a valid type & cksum
      } else skippy(TT.hdr.size);

      continue;
    }

    // At this point, we're writing something to the filesystem. Parse fields.
    TT.hdr.mode = otoi(tar.mode, sizeof(tar.mode));
    TT.hdr.mode |= (char []){8,8,10,2,6,4,1,8}[tar.type-'0']<<12;
    TT.hdr.uid = otoi(tar.uid, sizeof(tar.uid));
    TT.hdr.gid = otoi(tar.gid, sizeof(tar.gid));
    TT.hdr.mtime = otoi(tar.mtime, sizeof(tar.mtime));
    TT.hdr.device = dev_makedev(otoi(tar.major, sizeof(tar.major)),
      otoi(tar.minor, sizeof(tar.minor)));

    TT.hdr.uname = xstrndup(tar.uname, sizeof(tar.uname));
    TT.hdr.gname = xstrndup(tar.gname, sizeof(tar.gname));
    if (!TT.hdr.link_target && *tar.link)
      TT.hdr.link_target = xstrndup(tar.link, sizeof(tar.link));
    if (!TT.hdr.name) {
      i = strnlen(tar.prefix, sizeof(tar.prefix));
      TT.hdr.name = xmprintf("%.*s%s%.*s", i, tar.prefix,
        (i && tar.prefix[i-1] != '/') ? "/" : "",
        (int)sizeof(tar.name), tar.name);
    }

    // Directories sometimes recorded as "file with trailing slash"
    if (S_ISREG(TT.hdr.mode) && (s = strend(TT.hdr.name, "/"))) {
      *s = 0;
      TT.hdr.mode = (TT.hdr.mode & ~S_IFMT) | S_IFDIR;
    }

    // Hardlinks, symlinks, and directories do not have contents in archive
    // (Neither do fifo, block or char devices, but not testing for that...?)
    if ((TT.hdr.link_target && *TT.hdr.link_target) 
        || S_ISLNK(TT.hdr.mode) || S_ISDIR(TT.hdr.mode))
      TT.hdr.size = 0;

    // Skip excluded files
    if (filter(TT.exc, TT.hdr.name) || (TT.inc && !filter(TT.inc, TT.hdr.name)))
      skippy(TT.hdr.size);
    else {

// TODO: wrong, shouldn't grow endlessly, mark seen TT.inc instead
      add_to_list(&TT.pass, xstrdup(TT.hdr.name));

      if (FLAG(t)) {
        if (FLAG(v)) {
          char perm[11];
          struct tm *lc = localtime(&TT.hdr.mtime);

          mode_to_string(TT.hdr.mode, perm);
          printf("%s %s/%s %9ld %d-%02d-%02d %02d:%02d:%02d ", perm,
              TT.hdr.uname, TT.hdr.gname, (long)TT.hdr.size, 1900+lc->tm_year,
              1+lc->tm_mon, lc->tm_mday, lc->tm_hour, lc->tm_min, lc->tm_sec);
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
    }

    free(TT.hdr.name);
    free(TT.hdr.link_target);
    free(TT.hdr.uname);
    free(TT.hdr.gname);
    TT.hdr.name = TT.hdr.link_target = 0;
  }
}

void tar_main(void)
{
  struct arg_list *tmp;
  char *s, **args = toys.optargs;

  // When extracting to command
  signal(SIGPIPE, SIG_IGN);

  if (!geteuid()) toys.optflags |= FLAG_p;

  // Collect file list
  while (*args) add_to_list(&TT.inc, *args++);
  for (;TT.T; TT.T = TT.T->next) file_to_list(TT.T->arg, &TT.inc);
  for (;TT.X; TT.X = TT.X->next) file_to_list(TT.X->arg, &TT.exc);

  // Open archive file
  if (FLAG(c)) {
    if (!TT.inc) error_exit("empty archive");
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
// TODO: autodtect

// Try detecting .gz or .bz2 by looking for their magic.
//      if ((!memcmp(tar.name, "\x1f\x8b", 2) || !memcmp(tar.name, "BZh", 3))
//          && !lseek(TT.fd, -i, SEEK_CUR)) {
//        toys.optflags |= (*tar.name == 'B') ? FLAG_j : FLAG_z;
//        extract_stream(tar_hdl);
//        continue;
//      }
    if (FLAG(j)||FLAG(z)) {
      int pipefd[2] = {TT.fd, -1};

      xpopen_both((char *[]){FLAG(z)?"gunzip":"bunzip2", "-cf", "-", NULL},
        pipefd);
      close(TT.fd);
      TT.fd = pipefd[1];
    }

    unpack_tar();
    for (tmp = TT.inc; tmp; tmp = tmp->next)
      if (!filter(TT.exc, tmp->arg) && !filter(TT.pass, tmp->arg))
        error_msg("'%s' not in archive", tmp->arg);

  // are we writing? (Don't have to test flag here one of 3 must be set)
  } else {
// TODO: autodetect
    if (FLAG(j)||FLAG(z)) {
      int pipefd[2] = {-1, TT.fd};

      xpopen_both((char *[]){FLAG(z)?"gzip":"bzip2", "-f", NULL}, pipefd);
      close(TT.fd);
      TT.fd = pipefd[0];
    }
    for (tmp = TT.inc; tmp; tmp = tmp->next)
      dirtree_flagread(tmp->arg, FLAG(h)?DIRTREE_SYMFOLLOW:0, add_to_tar);

    memset(toybuf, 0, 1024);
    writeall(TT.fd, toybuf, 1024);
  }
}
