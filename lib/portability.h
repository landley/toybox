// Workarounds for horrible build environment idiosyncrasies.

// Instead of polluting the code with strange #ifdefs to work around bugs
// in specific compiler, library, or OS versions, localize all that here
// and in portability.c

// Always use long file support.
// This must come before we #include any system header file to take effect!
#define _FILE_OFFSET_BITS 64

// For musl
#define _ALL_SOURCE
#include <regex.h>
#ifndef REG_STARTEND
#define REG_STARTEND 0
#endif

#ifdef __APPLE__
// macOS 10.13 doesn't have the POSIX 2008 direct access to timespec in
// struct stat, but we can ask it to give us something equivalent...
// (This must come before any #include!)
#define _DARWIN_C_SOURCE
// ...and then use macros to paper over the difference.
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#define st_mtim st_mtimespec
#endif

// Test for gcc (using compiler builtin #define)

#ifdef __GNUC__
#define printf_format	__attribute__((format(printf, 1, 2)))
#else
#define printf_format
#endif

// This isn't in the spec, but it's how we determine what libc we're using.

// Types various replacement prototypes need.
// This also lets us determine what libc we're using. Systems that
// have <features.h> will transitively include it, and ones that don't --
// macOS -- won't break.
#include <sys/types.h>

// Various constants old build environments might not have even if kernel does

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

#ifndef RLIMIT_RTTIME
#define RLIMIT_RTTIME 15
#endif

// Introduced in Linux 3.1
#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE 4
#endif

// We don't define GNU_dammit because we're not part of the gnu project, and
// don't want to get any FSF on us. Unfortunately glibc (gnu libc)
// won't give us Linux syscall wrappers without claiming to be part of the
// gnu project (because Stallman's "GNU owns Linux" revisionist history
// crusade includes the kernel, even though Linux was inspired by Minix).

// We use most non-posix Linux syscalls directly through the syscall() wrapper,
// but even many posix-2008 functions aren't provided by glibc unless you
// claim it's in the name of Gnu.

#if defined(__GLIBC__)
// "Function prototypes shall be provided." but aren't.
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/unistd.h.html
char *crypt(const char *key, const char *salt);

// According to posix, #include header, get a function definition. But glibc...
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/wcwidth.html
#include <wchar.h>
int wcwidth(wchar_t wc);

// see http://pubs.opengroup.org/onlinepubs/9699919799/functions/strptime.html
#include <time.h>
char *strptime(const char *buf, const char *format, struct tm *tm);

// They didn't like posix basename so they defined another function with the
// same name and if you include libgen.h it #defines basename to something
// else (where they implemented the real basename), and that define breaks
// the table entry for the basename command. They didn't make a new function
// with a different name for their new behavior because gnu.
//
// Solution: don't use their broken header, provide an inline to redirect the
// correct name to the broken name.

char *dirname(char *path);
char *__xpg_basename(char *path);
static inline char *basename(char *path) { return __xpg_basename(path); }
char *strcasestr(const char *haystack, const char *needle);
void *memmem(const void *haystack, size_t haystack_length,
  const void *needle, size_t needle_length);
#endif // defined(glibc)

// getopt_long(), getopt_long_only(), and struct option.
#include <getopt.h>

#if !defined(__GLIBC__)
// POSIX basename.
#include <libgen.h>
#endif

// Work out how to do endianness

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#ifdef __BIG_ENDIAN__
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__FreeBSD__)

#include <sys/endian.h>

#if _BYTE_ORDER == _BIG_ENDIAN
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

#else

#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

#endif

#if IS_BIG_ENDIAN
#define IS_LITTLE_ENDIAN 0
#define SWAP_BE16(x) (x)
#define SWAP_BE32(x) (x)
#define SWAP_BE64(x) (x)
#define SWAP_LE16(x) bswap_16(x)
#define SWAP_LE32(x) bswap_32(x)
#define SWAP_LE64(x) bswap_64(x)
#else
#define IS_LITTLE_ENDIAN 1
#define SWAP_BE16(x) bswap_16(x)
#define SWAP_BE32(x) bswap_32(x)
#define SWAP_BE64(x) bswap_64(x)
#define SWAP_LE16(x) (x)
#define SWAP_LE32(x) (x)
#define SWAP_LE64(x) (x)
#endif

// Linux headers not listed by POSIX or LSB
#include <sys/mount.h>
#ifdef __linux__
#include <sys/statfs.h>
#include <sys/swap.h>
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <util.h>
#elif !defined(__FreeBSD__)
#include <pty.h>
#else
#include <termios.h>
#ifndef IUTF8
#define IUTF8 0
#endif
#endif

#if defined(__APPLE__) || defined(__linux__)
// Linux and macOS has both have getxattr and friends in <sys/xattr.h>, but
// they aren't compatible.
#include <sys/xattr.h>
ssize_t xattr_get(const char *, const char *, void *, size_t);
ssize_t xattr_lget(const char *, const char *, void *, size_t);
ssize_t xattr_fget(int fd, const char *, void *, size_t);
ssize_t xattr_list(const char *, char *, size_t);
ssize_t xattr_llist(const char *, char *, size_t);
ssize_t xattr_flist(int, char *, size_t);
ssize_t xattr_set(const char*, const char*, const void*, size_t, int);
ssize_t xattr_lset(const char*, const char*, const void*, size_t, int);
ssize_t xattr_fset(int, const char*, const void*, size_t, int);
#endif

// macOS doesn't have these functions, but we can fake them.
#ifdef __APPLE__
int mknodat(int, const char*, mode_t, dev_t);
int posix_fallocate(int, off_t, off_t);
#endif

// Android is missing some headers and functions
// "generated/config.h" is included first
#if CFG_TOYBOX_SHADOW
#include <shadow.h>
#endif
#if CFG_TOYBOX_UTMPX
#include <utmpx.h>
#else
struct utmpx {int ut_type;};
#define USER_PROCESS 0
static inline struct utmpx *getutxent(void) {return 0;}
static inline void setutxent(void) {;}
static inline void endutxent(void) {;}
#endif

// Some systems don't define O_NOFOLLOW, and it varies by architecture, so...
#include <fcntl.h>
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_NOATIME
#define O_NOATIME 01000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif
#ifndef O_PATH
#define O_PATH   010000000
#endif
#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK (1<<30)
#endif

// Glibc won't give you linux-kernel constants unless you say "no, a BUD lite"
// even though linux has nothing to do with the FSF and never has.
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

#ifndef F_GETPIPE_SZ
#define F_GETPIPE_SZ 1032
#endif

#if defined(__SIZEOF_DOUBLE__) && defined(__SIZEOF_LONG__) \
    && __SIZEOF_DOUBLE__ <= __SIZEOF_LONG__
typedef double FLOAT;
#else
typedef float FLOAT;
#endif

#ifndef __uClinux__
pid_t xfork(void);
#endif

//#define strncpy(...) @@strncpyisbadmmkay@@
//#define strncat(...) @@strncatisbadmmkay@@

// Support building the Android tools on glibc, so hermetic AOSP builds can
// use toybox before they're ready to switch to host bionic.
#ifdef __BIONIC__
#include <android/log.h>
#else
typedef enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
} android_LogPriority;
static inline int __android_log_write(int pri, const char *tag, const char *msg)
{
  return -1;
}
#endif

// libprocessgroup is an Android platform library not included in the NDK.
#if defined(__BIONIC__)
#if __has_include(<processgroup/sched_policy.h>)
#include <processgroup/sched_policy.h>
#define GOT_IT
#endif
#endif
#ifdef GOT_IT
#undef GOT_IT
#else
static inline int get_sched_policy(int tid, void *policy) {return 0;}
static inline char *get_sched_policy_name(int policy) {return "unknown";}
#endif

// Android NDKv18 has liblog.so but not liblog.c for static builds,
// stub it out for now.
#ifdef __ANDROID_NDK__
#define __android_log_write(a, b, c) (0)
#endif

#ifndef SYSLOG_NAMES
typedef struct {char *c_name; int c_val;} CODE;
extern CODE prioritynames[], facilitynames[];
#endif

#if CFG_TOYBOX_GETRANDOM
#include <sys/random.h>
#endif
int xgetrandom(void *buf, unsigned len, unsigned flags);

// Android's bionic libc doesn't have confstr.
#ifdef __BIONIC__
#define _CS_PATH	0
#define _CS_V7_ENV	1
#include <string.h>
static inline void confstr(int a, char *b, int c) {strcpy(b, a ? "POSIXLY_CORRECT=1" : "/bin:/usr/bin");}
#endif

// Paper over the differences between BSD kqueue and Linux inotify for tail.

struct xnotify {
  char **paths;
  int max, *fds, count, kq;
};

struct xnotify *xnotify_init(int max);
int xnotify_add(struct xnotify *not, int fd, char *path);
int xnotify_wait(struct xnotify *not, char **path);

#ifdef __APPLE__
#define f_frsize f_iosize
#endif

int sig_to_num(char *s);
char *num_to_sig(int sig);

struct signame {
  int num;
  char *name;
};
void xsignal_all_killers(void *handler);

// Different OSes encode major/minor device numbers differently.
int dev_minor(int dev);
int dev_major(int dev);
int dev_makedev(int major, int minor);

char *fs_type_name(struct statfs *statfs);

int get_block_device_size(int fd, unsigned long long *size);
