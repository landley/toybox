// Workarounds for horrible build environment idiosyncrasies.

// Instead of polluting the code with strange #ifdefs to work around bugs
// in specific compiler, library, or OS versions, localize all that here
// and in portability.c

// For musl
#define _ALL_SOURCE

// Test for gcc (using compiler builtin #define)

#ifdef __GNUC__
#define noreturn	__attribute__((noreturn))
#define printf_format	__attribute__((format(printf, 1, 2)))
#else
#define noreturn
#define printf_format
#endif

// Always use long file support.
#define _FILE_OFFSET_BITS 64

// This isn't in the spec, but it's how we determine what libc we're using.

#include <features.h>

// Types various replacement prototypes need
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

// When building under obsolete glibc (Ubuntu 8.04-ish), hold its hand a bit.
#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 10
#define fstatat fstatat64
int fstatat64(int dirfd, const char *pathname, void *buf, int flags);
int readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
char *stpcpy(char *dest, const char *src);
#include <sys/stat.h>
int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int openat(int dirfd, const char *pathname, int flags, ...);
#include <dirent.h>
DIR *fdopendir(int fd);
#include <unistd.h>
int fchownat(int dirfd, const char *pathname,
                    uid_t owner, gid_t group, int flags);
int isblank(int c);
int unlinkat(int dirfd, const char *pathname, int flags);
#include <stdio.h>
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

// Straight from posix-2008, things old glibc had but didn't prototype

int faccessat(int fd, const char *path, int amode, int flag);
int linkat(int fd1, const char *path1, int fd2, const char *path2, int flag);
int mkdirat(int fd, const char *path, mode_t mode);
int symlinkat(const char *path1, int fd, const char *path2);
int mknodat(int fd, const char *path, mode_t mode, dev_t dev);
#include <sys/time.h>
int futimens(int fd, const struct timespec times[2]);
int utimensat(int fd, const char *path, const struct timespec times[2], int flag);

#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif
#endif // Old glibc

#endif // glibc in general

#if !defined(__GLIBC__)
// POSIX basename.
#include <libgen.h>
#endif

// Work out how to do endianness

#ifndef __APPLE__
#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

int clearenv(void);
#else

#ifdef __BIG_ENDIAN__
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

#if defined(__APPLE__) \
    || (defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ < 10)
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif

// Linux headers not listed by POSIX or LSB
#include <sys/mount.h>
#include <sys/swap.h>

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
#include <sys/system_properties.h>
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
#define PROP_VALUE_MAX 92
static inline int __system_property_set(const char *key, const char *value)
{
  return -1;
}
#endif

#if defined(__BIONIC__)
#if defined(__ANDROID_NDK__)
static inline int get_sched_policy(int tid, void *policy) {return 0;}
static inline char *get_sched_policy_name(int policy) {return "unknown";}
#else
#include <cutils/sched_policy.h>
#endif
#endif

#ifndef SYSLOG_NAMES
typedef struct {char *c_name; int c_val;} CODE;
extern CODE prioritynames[], facilitynames[];
#endif

#if CFG_TOYBOX_GETRANDOM
#include <sys/random.h>
#endif
void xgetrandom(void *buf, unsigned len, unsigned flags);

// Android's bionic libc doesn't have confstr.
#ifdef __BIONIC__
#define _CS_PATH	0
#define _CS_V7_ENV	1
#include <string.h>
static inline void confstr(int a, char *b, int c) {strcpy(b, a ? "POSIXLY_CORRECT=1" : "/bin:/usr/bin");}
#endif
