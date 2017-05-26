/* tail.c - copy last lines from input to stdout.
 *
 * Copyright 2012 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/tail.html
 *
 * Deviations from posix: -f waits for pipe/fifo on stdin (nonblock?).

USE_TAIL(NEWTOY(tail, "?fc-n-[-cn]", TOYFLAG_USR|TOYFLAG_BIN))

config TAIL
  bool "tail"
  default y
  help
    usage: tail [-n|c NUMBER] [-f] [FILE...]

    Copy last lines from files to stdout. If no files listed, copy from
    stdin. Filename "-" is a synonym for stdin.

    -n	output the last NUMBER lines (default 10), +X counts from start
    -c	output the last NUMBER bytes, +NUMBER counts from start
    -f	follow FILE(s), waiting for more data to be appended

config TAIL_SEEK
  bool "tail seek support"
  default y
  depends on TAIL
  help
    This version uses lseek, which is faster on large files.
*/

#define FOR_tail
#include "toys.h"
#include <sys/inotify.h>

GLOBALS(
  long lines;
  long bytes;

  int file_no, ffd, *files;
)

struct line_list {
  struct line_list *next, *prev;
  char *data;
  int len;
};

static struct line_list *get_chunk(int fd, int len)
{
  struct line_list *line = xmalloc(sizeof(struct line_list)+len);

  memset(line, 0, sizeof(struct line_list));
  line->data = ((char *)line) + sizeof(struct line_list);
  line->len = readall(fd, line->data, len);

  if (line->len < 1) {
    free(line);
    return 0;
  }

  return line;
}

static void dump_chunk(void *ptr)
{
  struct line_list *list = ptr;

  xwrite(1, list->data, list->len);
  free(list);
}

// Reading through very large files is slow.  Using lseek can speed things
// up a lot, but isn't applicable to all input (cat | tail).
// Note: bytes and lines are negative here.
static int try_lseek(int fd, long bytes, long lines)
{
  struct line_list *list = 0, *temp;
  int flag = 0, chunk = sizeof(toybuf);
  off_t pos = lseek(fd, 0, SEEK_END);

  // If lseek() doesn't work on this stream, return now.
  if (pos<0) return 0;

  // Seek to the right spot, output data from there.
  if (bytes) {
    if (lseek(fd, bytes, SEEK_END)<0) lseek(fd, 0, SEEK_SET);
    xsendfile(fd, 1);
    return 1;
  }

  // Read from end to find enough lines, then output them.

  bytes = pos;
  while (lines && pos) {
    int offset;

    // Read in next chunk from end of file
    if (chunk>pos) chunk = pos;
    pos -= chunk;
    if (pos != lseek(fd, pos, SEEK_SET)) {
      perror_msg("seek failed");
      break;
    }
    if (!(temp = get_chunk(fd, chunk))) break;
    temp->next = list;
    list = temp;

    // Count newlines in this chunk.
    offset = list->len;
    while (offset--) {
      // If the last line ends with a newline, that one doesn't count.
      if (!flag) flag++;

      // Start outputting data right after newline
      else if (list->data[offset] == '\n' && !++lines) {
        offset++;
        list->data += offset;
        list->len -= offset;

        break;
      }
    }
  }

  // Output stored data
  llist_traverse(list, dump_chunk);

  // In case of -f
  lseek(fd, bytes, SEEK_SET);
  return 1;
}

// Called for each file listed on command line, and/or stdin
static void do_tail(int fd, char *name)
{
  long bytes = TT.bytes, lines = TT.lines;
  int linepop = 1;

  if (toys.optflags & FLAG_f) {
    int f = TT.file_no*2;
    char *s = name;

    if (!fd) sprintf(s = toybuf, "/proc/self/fd/%d", fd);
    TT.files[f++] = fd;
    if (0 > (TT.files[f] = inotify_add_watch(TT.ffd, s, IN_MODIFY)))
      perror_msg("bad -f on '%s'", name);
  }

  if (TT.file_no++) xputc('\n');
  if (toys.optc > 1) xprintf("==> %s <==\n", name);

  // Are we measuring from the end of the file?

  if (bytes<0 || lines<0) {
    struct line_list *list = 0, *new;

    // The slow codepath is always needed, and can handle all input,
    // so make lseek support optional.
    if (CFG_TAIL_SEEK && try_lseek(fd, bytes, lines)) return;

    // Read data until we run out, keep a trailing buffer
    for (;;) {
      // Read next page of data, appending to linked list in order
      if (!(new = get_chunk(fd, sizeof(toybuf)))) break;
      dlist_add_nomalloc((void *)&list, (void *)new);

      // If tracing bytes, add until we have enough, discarding overflow.
      if (TT.bytes) {
        bytes += new->len;
        if (bytes > 0) {
          while (list->len <= bytes) {
            bytes -= list->len;
            free(dlist_pop(&list));
          }
          list->data += bytes;
          list->len -= bytes;
          bytes = 0;
        }
      } else {
        int len = new->len, count;
        char *try = new->data;

        // First character _after_ a newline starts a new line, which
        // works even if file doesn't end with a newline
        for (count=0; count<len; count++) {
          if (linepop) lines++;
          linepop = try[count] == '\n';

          if (lines > 0) {
            char c;

            do {
              c = *list->data;
              if (!--(list->len)) free(dlist_pop(&list));
              else list->data++;
            } while (c != '\n');
            lines--;
          }
        }
      }
    }

    // Output/free the buffer.
    llist_traverse(list, dump_chunk);

  // Measuring from the beginning of the file.
  } else for (;;) {
    int len, offset = 0;

    // Error while reading does not exit.  Error writing does.
    len = read(fd, toybuf, sizeof(toybuf));
    if (len<1) break;
    while (bytes > 1 || lines > 1) {
      bytes--;
      if (toybuf[offset++] == '\n') lines--;
      if (offset >= len) break;
    }
    if (offset<len) xwrite(1, toybuf+offset, len-offset);
  }
}

void tail_main(void)
{
  char **args = toys.optargs;

  if (!(toys.optflags&(FLAG_n|FLAG_c))) {
    char *arg = *args;

    // handle old "-42" style arguments
    if (arg && *arg == '-' && arg[1]) {
      TT.lines = atolx(*(args++));
      toys.optc--;
    } else {
      // if nothing specified, default -n to -10
      TT.lines = -10;
    }
  }

  // Allocate 2 ints per optarg for -f
  if (toys.optflags&FLAG_f) {
    if ((TT.ffd = inotify_init()) < 0) perror_exit("inotify_init");
    TT.files = xmalloc(toys.optc*8);
  }
  loopfiles_rw(args, O_RDONLY|WARN_ONLY|(O_CLOEXEC*!(toys.optflags&FLAG_f)),
    0, do_tail);

  if ((toys.optflags & FLAG_f) && TT.file_no) {
    int len, last_fd = TT.files[(TT.file_no-1)*2], i, fd;
    struct inotify_event ev;

    for (;;) {
      if (sizeof(ev)!=read(TT.ffd, &ev, sizeof(ev))) perror_exit("inotify");

      for (i = 0; i<TT.file_no && ev.wd!=TT.files[(i*2)+1]; i++);
      if (i==TT.file_no) continue;
      fd = TT.files[i*2];

      // Read new data.
      while ((len = read(fd, toybuf, sizeof(toybuf)))>0) {
        if (last_fd != fd) {
          last_fd = fd;
          xprintf("\n==> %s <==\n", args[i]);
        }

        xwrite(1, toybuf, len);
      }
    }
  }
}
