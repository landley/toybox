/* stat.c : display file or file system status
 * anand.sinha85@gmail.com
 * Copyright 2012 <warior.linux@gmail.com>

USE_STAT(NEWTOY(stat, "LZfc", TOYFLAG_BIN)) 

config STAT
    bool stat
    default n
    help
        Usage: stat [OPTION] FILE...
        display file or file system status
        -Z, --context
             print the security context information if available
        -f, --file-system
            display file system status instead of file status
        -c  --format=FORMAT
            use the specified FORMAT instead of the default; output a newline after each use of FORMAT
        --help display this help and exit
        The valid format sequences for files (without --file-system):
        %a     Access rights in octal
        %A     Access rights in human readable form
        %b     Number of blocks allocated (see
        %B     The size in bytes of each block reported by
        %d     Device number in decimal
        %D     Device number in hex
        %f     Raw mode in hex
        %F     File type
        %G     Group name of owner
        %h     Number of hard links
        %i     Inode number
        %n     File name
        %N     Quoted file name with dereference if symbolic link
        %o     I/O block size
        %s     Total size, in bytes
        %t     Major device type in hex
        %T     Minor device type in hex
        %u     User ID of owner
        %U     User name of owner
        %x     Time of last access
        %X     Time of last access as seconds since Epoch
        %y     Time of last modification
        %Y     Time of last modification as seconds since Epoch
        %z     Time of last change
        %Z     Time of last change as seconds since Epoch
*/

#define FOR_stat
#include "toys.h"

#define SIZE_DATE_TIME_STAT 36
#define access_string(x, s, i)  if((x&7) & 1)           \
                                s[9 - i * 3] = 'x';       \
                           else                             \
                               s[9 - i * 3] = '-';             \
                           if(((x&7) >> 1) & 1)                 \
                               s[9 - (i * 3 + 1)] = 'w';            \
                           else                     \
                               s[9 - (i * 3 + 1)] = '-';            \
                           if(((x&7) >> 2) & 1)     \
                               s[9 - (i * 3 + 2)] = 'r';            \
                           else                     \
                               s[9 - (i * 3 + 2)] = '-';

static char *check_type_file(mode_t, size_t);
static char *get_access_str(unsigned long, mode_t);
static char *date_stat_format(time_t );
inline void print_stat_format(char *, int);

GLOBALS(
	char *access_str;
	char *file_type;
	struct passwd *user_name;
	struct group *group_name;
	struct tm *time_toy;
	struct stat *toystat;
	struct statfs *toystatfs;
	int toy_obj_file_arg;
)


static void do_stat(const char * file_name)
{
    TT.toystat = xmalloc(sizeof(struct stat));
    if (stat(file_name, TT.toystat) < 0) perror_msg("stat: '%s'", file_name);
}

static void do_statfs(const char * file_name)
{
    TT.toystatfs = xmalloc(sizeof(struct statfs));
    if (statfs(file_name, TT.toystatfs) < 0)
        perror_msg("statfs: '%s'", file_name);
}

static char * check_type_file(mode_t mode, size_t size)
{
    if (S_ISREG(mode)) {
        if (size) return "regular file";
        return "regular empty file";
    }
    if (S_ISDIR(mode)) return "directory"; 
    if (S_ISCHR(mode)) return "character device";
    if (S_ISBLK(mode)) return "block device";
    if (S_ISFIFO(mode)) return "FIFO (named pipe)";
    if (S_ISLNK(mode)) return "symbolic link";
    if (S_ISSOCK(mode)) return "socket";
}

static char * get_access_str(unsigned long pernission, mode_t mode)
{
    static char access_string[10];
    int i;

    if (S_ISDIR(mode)) access_string[0] = 'd';
    else access_string[0] = '-';
    for (i = 0; i < 3; i++)
        access_string(pernission >> (i * 3) & 7, access_string, i);

    access_string[10] = '\0';
    return access_string;
}

static char * date_stat_format(time_t time)
{
    static char buf[SIZE_DATE_TIME_STAT];

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.000000000", localtime(&time));
    return buf;
}

inline void print_stat_format(char *format, int flag)
{
    format++;
    switch (*format) {
        case 'a':
            if (flag) xprintf("%lu\n", TT.toystatfs->f_bavail);
            else xprintf("%04lo\n",TT.toystat->st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO));
            break;
        case 'A':
            xprintf("%s\n",TT.access_str);
            break;
        case 'b':
            if (flag) xprintf("%lu\n", TT.toystatfs->f_blocks);
            else xprintf("%llu\n", TT.toystat->st_blocks);
            break;
        case 'B':
            xprintf("%lu\n", TT.toystat->st_blksize);
            break;
        case 'c':
            if (flag) xprintf("%lu\n", TT.toystatfs->f_files);
            break;
        case 'C':
            xprintf("Currently feature is not supported\n");
            break;
        case 'd':
            if (flag) xprintf("%lu\n", TT.toystatfs->f_ffree);
            else xprintf("%ldd\n", TT.toystat->st_dev);
            break;
        case 'D':
            xprintf("%llxh\n", TT.toystat->st_dev);
            break;
        case 'f':
            if (flag) xprintf("%lu\n", TT.toystatfs->f_bfree);
            else xprintf("%lx\n", TT.toystat->st_mode);
            break;
        case 'F':
            xprintf("%s\n", TT.file_type);
            break;
        case 'g':
            xprintf("%lu\n", TT.toystat->st_uid);
            break;
        case 'G':
            xprintf("%8s\n", TT.user_name->pw_name);
            break;
        case 'h':
            xprintf("%lu\n", TT.toystat->st_nlink);
            break;
        case 'i':
            if (flag)
                xprintf("%d%d\n", TT.toystatfs->f_fsid.__val[0], TT.toystatfs->f_fsid.__val[1]);
            else xprintf("%llu\n", TT.toystat->st_ino);
            break;
        case 'l':
            if (flag) xprintf("need to implement\n");
            break;
        case 'n':
            xprintf("%s\n", toys.optargs[TT.toy_obj_file_arg]);
            break;
        case 'N':
            xprintf("`%s\n'", toys.optargs[TT.toy_obj_file_arg]);
            break;
        case 'o':
            xprintf("%lu\n", TT.toystat->st_blksize);
            break;
        case 's':
            if (flag) xprintf("%d\n", TT.toystatfs->f_frsize);
            else xprintf("%llu\n", TT.toystat->st_size);
            break;
        case 'S':
            if (flag) xprintf("%d\n", TT.toystatfs->f_bsize);
            break;
        case 't':
            if (flag) xprintf("%lx\n", TT.toystatfs->f_type);
            break;
        case 'T':
            if (flag) xprintf("Needs to be implemented\n");
            break;
        case 'u':
            xprintf("%lu\n", TT.toystat->st_uid);
            break;
        case 'U':
            xprintf("%8s\n", TT.user_name->pw_name);
            break;
        case 'x':
            xprintf("%s\n", date_stat_format(TT.toystat->st_atime));
            break;
        case 'X':
            xprintf("%llu\n", TT.toystat->st_atime);
            break;
        case 'y':
            xprintf("%s\n", date_stat_format(TT.toystat->st_mtime));
            break;
        case 'Y':
            xprintf("%llu\n", TT.toystat->st_mtime);
            break;
        case 'z':
            xprintf("%s\n", date_stat_format(TT.toystat->st_ctime));
            break;
        case 'Z':
            xprintf("%llu\n", TT.toystat->st_ctime);
        default:
            xprintf("%c\n", *format);
            break;
    }
    exit(0);
}

void stat_main(void)
{
    int stat_flag_Z = 0, stat_flag_f = 0, stat_flag_c = 0, stat_format = 0;

    if (toys.optargs) {
        if (toys.optflags & 1) {
            stat_flag_c = 1;
            TT.toy_obj_file_arg = 1;
            stat_format = 1;
        }
        if (toys.optflags & (1 << 1)) {
            stat_flag_f = 1;
            do_statfs(toys.optargs[TT.toy_obj_file_arg]);
        } else do_stat(toys.optargs[TT.toy_obj_file_arg]);
        if (toys.optflags & (1 << 2)) {
            stat_flag_Z = 1;
            xprintf("SELinux feature has not been implemented so far..\n");
        }
    }
// function to check the type/mode of file
    if (!stat_flag_f) {
        TT.file_type = check_type_file(TT.toystat->st_mode, TT.toystat->st_size);
// check user and group name
        TT.user_name = getpwuid(TT.toystat->st_uid);
        TT.group_name = getgrgid(TT.toystat->st_gid);
// function to get access in human readable format
        TT.access_str = get_access_str((TT.toystat->st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)), TT.toystat->st_mode);
        TT.time_toy = gmtime(&(TT.toystat->st_atime));
    }
    if (!(stat_flag_f |stat_flag_Z)) {
        if (stat_format) print_stat_format(toys.optargs[0], stat_flag_f);
        xprintf(" File: `%s'\n", toys.optargs[TT.toy_obj_file_arg]);
        xprintf(" Size: %llu\t Blocks: %llu\t IO Blocks: %lu\t", TT.toystat->st_size, TT.toystat->st_blocks, TT.toystat->st_blksize);
        xprintf("%s\n", TT.file_type);
        xprintf("Device: %llxh\t Inode: %llu\t Links: %lu\n", TT.toystat->st_dev, TT.toystat->st_ino, TT.toystat->st_nlink);
        xprintf("Access: (%04lo/%s)\tUid: (%lu/%8s)\tGid: (%lu/%8s)\n", (TT.toystat->st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)), TT.access_str, TT.toystat->st_uid, TT.user_name->pw_name, TT.toystat->st_gid, TT.group_name->gr_name);
        xprintf("Access: %s\nModify: %s\nChange: %s\n", date_stat_format(TT.toystat->st_atime), date_stat_format(TT.toystat->st_mtime), date_stat_format(TT.toystat->st_ctime));
    } else if (stat_flag_f) {
        // implementation of statfs -f, file system
        if (stat_format) print_stat_format(toys.optargs[0], stat_flag_f);
        xprintf(" File: \"%s\"\n", toys.optargs[TT.toy_obj_file_arg]);
        xprintf("   ID: %d%d Namelen: %ld    Type: %lx\n", TT.toystatfs->f_fsid.__val[0], TT.toystatfs->f_fsid.__val[1], TT.toystatfs->f_namelen, TT.toystatfs->f_type);
        xprintf("Block Size: %d    Fundamental block size: %d\n", TT.toystatfs->f_bsize, TT.toystatfs->f_frsize);
        xprintf("Blocks: Total: %lu\t", TT.toystatfs->f_blocks);
        xprintf("Free: %lu\t", TT.toystatfs->f_bfree);
        xprintf("Available: %lu\n", TT.toystatfs->f_bavail);
        xprintf("Inodes: Total: %lu\t", TT.toystatfs->f_files);
        xprintf("\tFree: %d\n", TT.toystatfs->f_ffree);
    }
}
