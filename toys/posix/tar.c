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
 *

USE_TAR(NEWTOY(tar, "&(restrict)(full-time)(no-recursion)(numeric-owner)(no-same-permissions)(overwrite)(exclude)*(mode):(mtime):(group):(owner):(to-command):o(no-same-owner)p(same-permissions)k(keep-old)c(create)|h(dereference)x(extract)|t(list)|v(verbose)I(use-compress-program):J(xz)j(bzip2)z(gzip)S(sparse)O(to-stdout)P(absolute-names)m(touch)X(exclude-from)*T(files-from)*C(directory):f(file):a[!txc][!jzJa]", TOYFLAG_USR|TOYFLAG_BIN))

config TAR
  bool "tar"
  default y
  help
    usage: tar [-cxt] [-fvohmjkOS] [-XTCf NAME] [FILE...]

    Create, extract, or list files in a .tar (or compressed t?z) file.

    Options:
    c  Create                x  Extract               t  Test (list)
    f  tar FILE (default -)  C  Change to DIR first   v  Verbose display
    o  Ignore owner          h  Follow symlinks       m  Ignore mtime
    J  xz compression        j  bzip2 compression     z  gzip compression
    O  Extract to stdout     X  exclude names in FILE T  include names in FILE

    --exclude        FILENAME to exclude    --full-time   Show seconds with -tv
    --mode MODE      Adjust modes           --mtime TIME  Override timestamps
    --owner NAME     Set file owner to NAME --group NAME  Set file group to NAME
    --sparse         Record sparse files
    --restrict       All archive contents must extract under one subdirectory
    --numeric-owner  Save/use/display uid and gid, not user/group name
    --no-recursion   Don't store directory contents
    -I PROG          Filter through PROG to compress or PROG -d to decompress
*/

#define FOR_tar
#include "toys.h"

GLOBALS(
  char *f, *C;
  struct arg_list *T, *X;
  char *I, *to_command, *owner, *group, *mtime, *mode;
  struct arg_list *exclude;

  struct double_list *incl, *excl, *seen;
  struct string_list *dirs;
  char *cwd;
  int fd, ouid, ggid, hlc, warn, adev, aino, sparselen;
  long long *sparse;
  time_t mtt;

  // hardlinks seen so far (hlc many)
  struct {
    char *arg;
    ino_t ino;
    dev_t dev;
  } *hlx;

  // Parsed information about a tar header.
  struct tar_header {
    char *name, *link_target, *uname, *gname;
    long long size, ssize;
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

// convert from int to octal (or base-256)
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
    while (len && *str == ' ') str++;
    while (len && *str>='0' && *str<='7') val = val*8+*str++-'0', len--;
    if (len && *str && *str != ' ') error_exit("bad header");
  }

  return val;
}
#define OTOI(x) otoi(x, sizeof(x))

static void write_longname(char *name, char type)
{
  struct tar_hdr tmp;
  int sz = strlen(name) +1;

  memset(&tmp, 0, sizeof(tmp));
  strcpy(tmp.name, "././@LongLink");
  ITOO(tmp.uid, 0);
  ITOO(tmp.gid, 0);
  ITOO(tmp.size, sz);
  ITOO(tmp.mtime, 0);
  tmp.type = type;
  strcpy(tmp.magic, "ustar  ");

  // Historical nonsense to match other implementations. Never used.
  ITOO(tmp.mode, 0644);
  strcpy(tmp.uname, "root");
  strcpy(tmp.gname, "root");

  // Calculate checksum. Since 512*255 = 0377000 in octal, this can never
  // use more than 6 digits. The last byte is ' ' for historical reasons.
  itoo(tmp.chksum, sizeof(tmp.chksum)-1, tar_cksum(&tmp));
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
    // constant is FNM_LEADING_DIR
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
  char **b = buf;

  free(*b);
  *b = xmalloc(len+1);
  xreadall(TT.fd, *b, len);
  (*b)[len] = 0;
}

// callback from dirtree to create archive
static int add_to_tar(struct dirtree *node)
{
  struct stat *st = &(node->st);
  struct tar_hdr hdr;
  struct passwd *pw = pw;
  struct group *gr = gr;
  int i, fd = -1, norecurse = FLAG(no_recursion);
  char *name, *lnk, *hname;

  if (!dirtree_notdotdot(node)) return 0;
  if (TT.adev == st->st_dev && TT.aino == st->st_ino) {
    error_msg("'%s' file is the archive; not dumped", node->name);
    return 0;
  }

  i = 1;
  name = hname = dirtree_path(node, &i);

  // exclusion defaults to --no-anchored and --wildcards-match-slash
  for (lnk = name; *lnk;) {
    if (filter(TT.excl, lnk)) {
      norecurse++;

      goto done;
    }
    while (*lnk && *lnk!='/') lnk++;
    while (*lnk=='/') lnk++;
  }

  // Consume the 1 extra byte alocated in dirtree_path()
  if (S_ISDIR(st->st_mode) && name[i-1] != '/') strcat(name, "/");

  // remove leading / and any .. entries from saved name
  if (!FLAG(P)) while (*hname == '/') hname++;
  for (lnk = hname;;) {
    if (!(lnk = strstr(lnk, ".."))) break;
    if (lnk == hname || lnk[-1] == '/') {
      if (!lnk[2]) goto done;
      if (lnk[2]=='/') lnk = hname = lnk+3;
    } else lnk+= 2;
  }
  if (!*hname) goto done;

  if (TT.warn && hname != name) {
    fprintf(stderr, "removing leading '%.*s' from member names\n",
           (int)(hname-name), name);
    TT.warn = 0;
  }

  if (TT.owner) st->st_uid = TT.ouid;
  if (TT.group) st->st_gid = TT.ggid;
  if (TT.mode) st->st_mode = string_to_mode(TT.mode, st->st_mode);
  if (TT.mtime) st->st_mtime = TT.mtt;

  memset(&hdr, 0, sizeof(hdr));
  strncpy(hdr.name, hname, sizeof(hdr.name));
  ITOO(hdr.mode, st->st_mode &07777);
  ITOO(hdr.uid, st->st_uid);
  ITOO(hdr.gid, st->st_gid);
  ITOO(hdr.size, 0); //set size later
  ITOO(hdr.mtime, st->st_mtime);
  strcpy(hdr.magic, "ustar  ");

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
      // first time we've seen it. Store as normal file, but remember it.
      if (!(TT.hlc&255))
        TT.hlx = xrealloc(TT.hlx, sizeof(*TT.hlx)*(TT.hlc+256));
      TT.hlx[TT.hlc].arg = xstrdup(hname);
      TT.hlx[TT.hlc].ino = st->st_ino;
      TT.hlx[TT.hlc].dev = st->st_dev;
      TT.hlc++;
      i = 0;
    }
  } else i = 0;

  // Handle file types
  if (i || S_ISLNK(st->st_mode)) {
    hdr.type = '1'+!i;
    if (!i && !(lnk = xreadlink(name))) {
      perror_msg("readlink");
      goto done;
    }
    if (strlen(lnk) > sizeof(hdr.link)) write_longname(lnk, 'K');
    strncpy(hdr.link, lnk, sizeof(hdr.link));
    if (!i) free(lnk);
  } else if (S_ISREG(st->st_mode)) {
    hdr.type = '0';
    ITOO(hdr.size, st->st_size);
  } else if (S_ISDIR(st->st_mode)) hdr.type = '5';
  else if (S_ISFIFO(st->st_mode)) hdr.type = '6';
  else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
    hdr.type = (S_ISCHR(st->st_mode))?'3':'4';
    ITOO(hdr.major, dev_major(st->st_rdev));
    ITOO(hdr.minor, dev_minor(st->st_rdev));
  } else {
    error_msg("unknown file type '%o'", st->st_mode & S_IFMT);
    goto done;
  }

  if (strlen(hname) > sizeof(hdr.name)) write_longname(hname, 'L');

  if (!FLAG(numeric_owner)) {
    if (TT.owner || (pw = bufgetpwuid(st->st_uid)))
      strncpy(hdr.uname, TT.owner ? TT.owner : pw->pw_name, sizeof(hdr.uname));
    if (TT.group || (gr = bufgetgrgid(st->st_gid)))
      strncpy(hdr.gname, TT.group ? TT.group : gr->gr_name, sizeof(hdr.gname));
  }

  TT.sparselen = 0;
  if (hdr.type == '0') {
    // Before we write the header, make sure we can read the file
    if ((fd = open(name, O_RDONLY)) < 0) {
      perror_msg("can't open '%s'", name);

      return 0;
    }
    if (FLAG(S)) {
      long long lo, ld = 0, len = 0;

      // Enumerate the extents
      while ((lo = lseek(fd, ld, SEEK_HOLE)) != -1) {
        if (!(TT.sparselen&511))
          TT.sparse = xrealloc(TT.sparse, (TT.sparselen+514)*sizeof(long long));
        if (ld != lo) {
          TT.sparse[TT.sparselen++] = ld;
          len += TT.sparse[TT.sparselen++] = lo-ld;
        }
        if (lo == st->st_size || (ld = lseek(fd, lo, SEEK_DATA)) < lo) break;
      }

      // If there were extents, change type to S record
      if (TT.sparselen>2) {
        TT.sparse[TT.sparselen++] = st->st_size;
        TT.sparse[TT.sparselen++] = 0;
        hdr.type = 'S';
        lnk = (char *)&hdr;
        for (i = 0; i<TT.sparselen && i<8; i++)
          itoo(lnk+386+12*i, 12, TT.sparse[i]);

        // Record if there's overflow records, change length to sparse length,
        // record apparent length
        if (TT.sparselen>8) lnk[482] = 1;
        itoo(lnk+483, 12, st->st_size);
        ITOO(hdr.size, len);
      } else TT.sparselen = 0;
      lseek(fd, 0, SEEK_SET);
    }
  }

  itoo(hdr.chksum, sizeof(hdr.chksum)-1, tar_cksum(&hdr));
  hdr.chksum[7] = ' ';

  if (FLAG(v)) dprintf((TT.fd==1) ? 2 : 1, "%s\n", hname);

  // Write header and data to archive
  xwrite(TT.fd, &hdr, 512);
  if (TT.sparselen>8) {
    char buf[512];

    // write extent overflow blocks
    for (i=8;;i++) {
      int j = (i-8)%42;

      if (!j || i==TT.sparselen) {
        if (i!=8) {
          if (i!=TT.sparselen) buf[504] = 1;
          xwrite(TT.fd, buf, 512);
        }
        if (i==TT.sparselen) break;
        memset(buf, 0, sizeof(buf));
      }
      itoo(buf+12*j, 12, TT.sparse[i]);
    }
  }
  TT.sparselen >>= 1;
  if (hdr.type == '0' || hdr.type == 'S') {
    if (hdr.type == '0') xsendfile_pad(fd, TT.fd, st->st_size);
    else for (i = 0; i<TT.sparselen; i++) {
      if (TT.sparse[i*2] != lseek(fd, TT.sparse[i*2], SEEK_SET))
        perror_msg("%s: seek %lld", name, TT.sparse[i*2]);
      xsendfile_pad(fd, TT.fd, TT.sparse[i*2+1]);
    }
    if (st->st_size%512) writeall(TT.fd, toybuf, (512-(st->st_size%512)));
    close(fd);
  }
done:
  free(name);

  return (DIRTREE_RECURSE|(FLAG(h)?DIRTREE_SYMFOLLOW:0))*!norecurse;
}

static void wsettime(char *s, long long sec)
{
  struct timespec times[2] = {{sec, 0},{sec, 0}};

  if (utimensat(AT_FDCWD, s, times, AT_SYMLINK_NOFOLLOW))
    perror_msg("settime %lld %s", sec, s);
}

// Do pending directory utimes(), NULL to flush all.
static int dirflush(char *name, int isdir)
{
  char *s = 0, *ss;

  // Barf if name not in TT.cwd
  if (name) {
    if (!(ss = s = xabspath(name, -1-isdir))) {
      error_msg("'%s' bad symlink", name);

      return 1;
    }
    if (TT.cwd[1] && (!strstart(&ss, TT.cwd) || (*ss && *ss!='/'))) {
      error_msg("'%s' %s not under '%s'", name, s, TT.cwd);
      free(s);

      return 1;
    }

    // --restrict means first entry extracted is what everything must be under
    if (FLAG(restrict)) {
      free(TT.cwd);
      TT.cwd = strdup(s);
      toys.optflags ^= FLAG_restrict;
    }
    // use resolved name so trailing / is stripped
    if (isdir) unlink(s);
  }

  // Set deferred utimes() for directories this file isn't under.
  // (Files must be depth-first ordered in tarball for this to matter.)
  while (TT.dirs) {

    // If next file is under (or equal to) this dir, keep waiting
    if (name && strstart(&ss, ss = s) && (!*ss || *ss=='/')) break;

    wsettime(TT.dirs->str+sizeof(long long), *(long long *)TT.dirs->str);
    free(llist_pop(&TT.dirs));
  }
  free(s);

  // name was under TT.cwd
  return 0;
}

// write data to file
static void sendfile_sparse(int fd)
{
  long long len, used = 0, sent;
  int i = 0, j;

  do {
    if (TT.sparselen) {
      // Seek past holes or fill output with zeroes.
      if (-1 == lseek(fd, len = TT.sparse[i*2], SEEK_SET)) {
        sent = 0;
        while (len) {
          // first/last 512 bytes used, rest left zeroes
          j = (len>3072) ? 3072 : len;
          if (j != writeall(fd, toybuf+512, j)) goto error;
          len -= j;
        }
      } else {
        sent = len;
        if (!(len = TT.sparse[i*2+1]) && ftruncate(fd, sent+len))
          perror_msg("ftruncate");
      }
      if (len+used>TT.hdr.size) error_exit("sparse overflow");
    } else len = TT.hdr.size;

    len -= sendfile_len(TT.fd, fd, len, &sent);
    used += sent;
    if (len) {
error:
      if (fd!=1) perror_msg(0);
      skippy(TT.hdr.size-used);

      break;
    }
  } while (++i<TT.sparselen);

  close(fd);
}

static void extract_to_disk(void)
{
  char *name = TT.hdr.name;
  int ala = TT.hdr.mode;

  if (dirflush(name, S_ISDIR(ala))) {
    if (S_ISREG(ala) && !TT.hdr.link_target) skippy(TT.hdr.size);
 
    return;
  }

  // create path before file if necessary
  if (strrchr(name, '/') && mkpath(name) && errno!=EEXIST)
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
      int fd = xcreate(name,
        WARN_ONLY|O_WRONLY|O_CREAT|(FLAG(overwrite)?O_TRUNC:O_EXCL),
        ala & 07777);
      if (fd != -1) sendfile_sparse(fd);
      else skippy(TT.hdr.size);
    }
  } else if (S_ISDIR(ala)) {
    if ((mkdir(name, 0700) == -1) && errno != EEXIST)
      return perror_msg("%s: can't create", TT.hdr.name);
  } else if (S_ISLNK(ala)) {
    if (symlink(TT.hdr.link_target, TT.hdr.name))
      return perror_msg("can't link '%s' -> '%s'", name, TT.hdr.link_target);
  } else if (mknod(name, ala, TT.hdr.device))
    return perror_msg("can't create '%s'", name);

  // Set ownership
  if (!FLAG(o) && !geteuid()) {
    int u = TT.hdr.uid, g = TT.hdr.gid;

    if (TT.owner) TT.hdr.uid = TT.ouid;
    else if (!FLAG(numeric_owner) && *TT.hdr.uname) {
      struct passwd *pw = getpwnam(TT.hdr.uname);
      if (pw && (TT.owner || !FLAG(numeric_owner))) TT.hdr.uid = pw->pw_uid;
    }

    if (TT.group) TT.hdr.gid = TT.ggid;
    else if (!FLAG(numeric_owner) && *TT.hdr.uname) {
      struct group *gr = getgrnam(TT.hdr.gname);
      if (gr) TT.hdr.gid = gr->gr_gid;
    }

    if (lchown(name, u, g)) perror_msg("chown %d:%d '%s'", u, g, name);;
  }

  if (!S_ISLNK(ala)) chmod(TT.hdr.name, FLAG(p) ? ala : ala&0777);

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
    } else wsettime(TT.hdr.name, TT.hdr.mtime);
  }
}

static void unpack_tar(char *first)
{
  struct double_list *walk, *delete;
  struct tar_hdr tar;
  int i, and = 0;
  unsigned maj, min;
  char *s;

  for (;;) {
    if (first) {
      memcpy(&tar, first, i = 512);
      first = 0;
    } else {
      // align to next block and read it
      if (TT.hdr.size%512) skippy(512-TT.hdr.size%512);
      i = readall(TT.fd, &tar, 512);
    }

    if (i && i!=512) error_exit("short header");

    // Two consecutive empty headers ends tar even if there's more data
    if (!i || !*tar.name) {
      if (!i || and++) return;
      TT.hdr.size = 0;
      continue;
    }
    // ensure null temination even of pathological packets
    tar.padd[0] = and = 0;

    // Is this a valid TAR header?
    if (!is_tar_header(&tar)) error_exit("bad header");
    TT.hdr.size = OTOI(tar.size);

    // If this header isn't writing something to the filesystem
    if ((tar.type<'0' || tar.type>'7') && tar.type!='S'
        && (*tar.magic && tar.type))
    {
      // Long name extension header?
      if (tar.type == 'K') alloread(&TT.hdr.link_target, TT.hdr.size);
      else if (tar.type == 'L') alloread(&TT.hdr.name, TT.hdr.size);
      else if (tar.type == 'x') {
        char *p, *buf = 0;
        int i, len, n = 0;

        // Posix extended record "LEN NAME=VALUE\n" format
        alloread(&buf, TT.hdr.size);
        for (p = buf; (p-buf)<TT.hdr.size; p += len) {
          i = sscanf(p, "%u path=%n", &len, &n);
          if (i<1 || len<4 || len>TT.hdr.size) {
            error_msg("bad header");
            break;
          }
          p[len-1] = 0;
          if (n) {
            TT.hdr.name = xstrdup(p+n);
            break;
          }
        }
        free(buf);

      // Ignore everything else.
      } else skippy(TT.hdr.size);

      continue;
    }

    // Handle sparse file type
    if (tar.type == 'S') {
      char sparse[512];
      int max = 8;

      // Load 4 pairs of offset/len from S block, plus 21 pairs from each
      // continuation block, list says where to seek/write sparse file contents
      TT.sparselen = 0;
      s = 386+(char *)&tar;
      *sparse = i = 0;

      for (;;) {
        if (!(TT.sparselen&511))
          TT.sparse = xrealloc(TT.sparse, (TT.sparselen+512)*sizeof(long long));

        // If out of data in block check continue flag, stop or load next block
        if (++i>max || !*s) {
          if (!(*sparse ? sparse[504] : ((char *)&tar)[482])) break;
          xreadall(TT.fd, s = sparse, 512);
          max = 41;
          i = 0;
        }
        // Load next entry
        TT.sparse[TT.sparselen++] = otoi(s, 12);
        s += 12;
      }

      // Odd number of entries (from corrupted tar) would be dropped here
      TT.sparselen /= 2;
      if (TT.sparselen)
        TT.hdr.ssize = TT.sparse[2*TT.sparselen-1]+TT.sparse[2*TT.sparselen-2];
    } else {
      TT.sparselen = 0;
      TT.hdr.ssize = TT.hdr.size;
    }

    // At this point, we have something to output. Convert metadata.
    TT.hdr.mode = OTOI(tar.mode)&0xfff;
    if (tar.type == 'S' || !tar.type) TT.hdr.mode |= 0x8000;
    else TT.hdr.mode |= (char []){8,8,10,2,6,4,1,8}[tar.type-'0']<<12;
    TT.hdr.uid = OTOI(tar.uid);
    TT.hdr.gid = OTOI(tar.gid);
    TT.hdr.mtime = OTOI(tar.mtime);
    maj = OTOI(tar.major);
    min = OTOI(tar.minor);
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
      i = (tar.type=='S') ? 0 : strnlen(tar.prefix, sizeof(tar.prefix));
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
    if ((TT.hdr.link_target && *TT.hdr.link_target)
      || (tar.type && !S_ISREG(TT.hdr.mode)))
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
        char perm[12], gname[12];

        mode_to_string(TT.hdr.mode, perm);
        printf("%s", perm);
        sprintf(perm, "%u", TT.hdr.uid);
        sprintf(gname, "%u", TT.hdr.gid);
        printf(" %s/%s ", *TT.hdr.uname ? TT.hdr.uname : perm,
          *TT.hdr.gname ? TT.hdr.gname : gname);
        if (tar.type=='3' || tar.type=='4') printf("%u,%u", maj, min);
        else printf("%9lld", TT.hdr.ssize);
        sprintf(perm, ":%02d", lc->tm_sec);
        printf("  %d-%02d-%02d %02d:%02d%s ", 1900+lc->tm_year, 1+lc->tm_mon,
          lc->tm_mday, lc->tm_hour, lc->tm_min, FLAG(full_time) ? perm : "");
      }
      printf("%s", TT.hdr.name);
      if (TT.hdr.link_target) printf(" -> %s", TT.hdr.link_target);
      xputc('\n');
      skippy(TT.hdr.size);
    } else {
      if (FLAG(v)) printf("%s\n", TT.hdr.name);
      if (FLAG(O)) sendfile_sparse(1);
      else if (FLAG(to_command)) {
        if (S_ISREG(TT.hdr.mode)) {
          int fd, pid;

          xsetenv("TAR_FILETYPE", "f");
          xsetenv(xmprintf("TAR_MODE=%o", TT.hdr.mode), 0);
          xsetenv(xmprintf("TAR_SIZE=%lld", TT.hdr.ssize), 0);
          xsetenv("TAR_FILENAME", TT.hdr.name);
          xsetenv("TAR_UNAME", TT.hdr.uname);
          xsetenv("TAR_GNAME", TT.hdr.gname);
          xsetenv(xmprintf("TAR_MTIME=%llo", (long long)TT.hdr.mtime), 0);
          xsetenv(xmprintf("TAR_UID=%o", TT.hdr.uid), 0);
          xsetenv(xmprintf("TAR_GID=%o", TT.hdr.gid), 0);

          pid = xpopen((char *[]){"sh", "-c", TT.to_command, NULL}, &fd, 0);
          // todo: short write exits tar here, other skips data.
          sendfile_sparse(fd);
          fd = xpclose_both(pid, 0);
          if (fd) error_msg("%d: Child returned %d", pid, fd);
        }
      } else extract_to_disk();
    }

    free(TT.hdr.name);
    free(TT.hdr.link_target);
    free(TT.hdr.uname);
    free(TT.hdr.gname);
    TT.hdr.name = TT.hdr.link_target = 0;
  }
}

// Add copy of filename (minus trailing \n and /) to dlist **
static void trim2list(void *list, char *pline)
{
  char *n = xstrdup(pline);
  int i = strlen(n);

  dlist_add(list, n);
  if (i && n[i-1]=='\n') i--;
  while (i && n[i-1] == '/') i--;
  n[i] = 0;
}

// do_lines callback, selects TT.incl or TT.excl based on call order
static void do_XT(char **pline, long len)
{
  if (pline) trim2list(TT.X ? &TT.excl : &TT.incl, *pline);
}

void tar_main(void)
{
  char *s, **args = toys.optargs,
    *archiver = FLAG(I) ? TT.I : (FLAG(z) ? "gzip" : (FLAG(J) ? "xz":"bzip2"));
  int len = 0;

  // Needed when extracting to command
  signal(SIGPIPE, SIG_IGN);

  // Get possible early errors out of the way
  if (!geteuid()) toys.optflags |= FLAG_p;
  if (TT.owner) TT.ouid = xgetuid(TT.owner);
  if (TT.group) TT.ggid = xgetgid(TT.group);
  if (TT.mtime) xparsedate(TT.mtime, &TT.mtt, (void *)&s, 1);

  // Collect file list.
  for (; TT.exclude; TT.exclude = TT.exclude->next)
    trim2list(&TT.excl, TT.exclude->arg);
  for (;TT.X; TT.X = TT.X->next) do_lines(xopenro(TT.X->arg), '\n', do_XT);
  for (args = toys.optargs; *args; args++) trim2list(&TT.incl, *args);
  for (;TT.T; TT.T = TT.T->next) do_lines(xopenro(TT.T->arg), '\n', do_XT);

  // If include file list empty, don't create empty archive
  if (FLAG(c)) {
    if (!TT.incl) error_exit("empty archive");
    TT.fd = 1;
  }

  // nommu reentry for nonseekable input skips this, parent did it for us
  if (toys.stacktop) {
    if (TT.f && strcmp(TT.f, "-"))
      TT.fd = xcreate(TT.f, TT.fd*(O_WRONLY|O_CREAT|O_TRUNC), 0666);
    // Get destination directory
    if (TT.C) xchdir(TT.C);
  }

  // Get destination directory
  TT.cwd = xabspath(s = xgetcwd(), 1);
  free(s);

  // Remember archive inode so we don't overwrite it or add it to itself
  {
    struct stat st;

    if (!fstat(TT.fd, &st)) {
      TT.aino = st.st_ino;
      TT.adev = st.st_dev;
    }
  }

  // Are we reading?
  if (FLAG(x)||FLAG(t)) {
    char *hdr = 0;

    // autodetect compression type when not specified
    if (!(FLAG(j)||FLAG(z)||FLAG(I)||FLAG(J))) {
      len = xread(TT.fd, hdr = toybuf+sizeof(toybuf)-512, 512);
      if (len!=512 || !is_tar_header(hdr)) {
        // detect gzip and bzip signatures
        if (SWAP_BE16(*(short *)hdr)==0x1f8b) toys.optflags |= FLAG_z;
        else if (!memcmp(hdr, "BZh", 3)) toys.optflags |= FLAG_j;
        else if (peek_be(hdr, 7) == 0xfd377a585a0000UL) toys.optflags |= FLAG_J;
        else error_exit("Not tar");

        // if we can seek back we don't need to loop and copy data
        if (!lseek(TT.fd, -len, SEEK_CUR)) hdr = 0;
      }
    }

    if (FLAG(j)||FLAG(z)||FLAG(I)||FLAG(J)) {
      int pipefd[2] = {hdr ? -1 : TT.fd, -1}, i, pid;
      struct string_list *zcat = FLAG(I) ? 0 : find_in_path(getenv("PATH"),
        FLAG(j) ? "bzcat" : FLAG(J) ? "xzcat" : "zcat");

      // Toybox provides more decompressors than compressors, so try them first
      xpopen_both(zcat ? (char *[]){zcat->str, 0} :
        (char *[]){archiver, "-d", 0}, pipefd);
      if (CFG_TOYBOX_FREE) llist_traverse(zcat, free);

      if (!hdr) {
        // If we could seek, child gzip inherited fd and we read its output
        close(TT.fd);
        TT.fd = pipefd[1];

      } else {

        // If we autodetected type but then couldn't lseek to put the data back
        // we have to loop reading data from TT.fd and pass it to gzip ourselves
        // (starting with the block of data we read to autodetect).

        // dirty trick: move gzip input pipe to stdin so child closes spare copy
        dup2(pipefd[0], 0);
        if (pipefd[0]) close(pipefd[0]);

        // Fork a copy of ourselves to handle extraction (reads from zip output
        // pipe, writes to stdout).
        pipefd[0] = pipefd[1];
        pipefd[1] = 1;
        pid = xpopen_both(0, pipefd);
        close(pipefd[1]);

        // loop writing collated data to zip proc
        xwrite(0, hdr, len);
        for (;;) {
          if ((i = read(TT.fd, toybuf, sizeof(toybuf)))<1) {
            close(0);
            xwaitpid(pid);
            return;
          }
          xwrite(0, toybuf, i);
        }
      }
    }

    unpack_tar(hdr);
    dirflush(0, 0);

    // Each time a TT.incl entry is seen it's moved to the end of the list,
    // with TT.seen pointing to first seen list entry. Anything between
    // TT.incl and TT.seen wasn't encountered in archive..
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

    // autodetect compression type based on -f name. (Use > to avoid.)
    if (TT.f && !FLAG(j) && !FLAG(z) && !FLAG(I) && !FLAG(J)) {
      char *tbz[] = {".tbz", ".tbz2", ".tar.bz", ".tar.bz2"};
      if (strend(TT.f, ".tgz") || strend(TT.f, ".tar.gz"))
        toys.optflags |= FLAG_z;
      if (strend(TT.f, ".txz") || strend(TT.f, ".tar.xz"))
        toys.optflags |= FLAG_J;
      else for (len = 0; len<ARRAY_LEN(tbz); len++)
        if (strend(TT.f, tbz[len])) toys.optflags |= FLAG_j;
    }

    if (FLAG(j)||FLAG(z)||FLAG(I)||FLAG(J)) {
      int pipefd[2] = {-1, TT.fd};

      xpopen_both((char *[]){archiver, 0}, pipefd);
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
    llist_traverse(TT.excl, llist_free_double);
    llist_traverse(TT.incl, llist_free_double);
    while(TT.hlc) free(TT.hlx[--TT.hlc].arg);
    free(TT.hlx);
    free(TT.cwd);
    close(TT.fd);
  }
}
