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

    -n	Output the last NUMBER lines (default 10), +X counts from start
    -c	Output the last NUMBER bytes, +NUMBER counts from start
    -f	Follow FILE(s), waiting for more data to be appended
*/

#define FOR_tail
#include "toys.h"

GLOBALS(
  long n, c;

  int file_no, last_fd;
  struct xnotify *not;
)

struct line_list {
  struct line_list *next, *prev;
  char *data;
  int len;
};

static struct line_list *read_chunk(int fd, int len)
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

static void write_chunk(void *ptr)
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
    if (!(temp = read_chunk(fd, chunk))) break;
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
  llist_traverse(list, write_chunk);

  // In case of -f
  lseek(fd, bytes, SEEK_SET);
  return 1;
}

// Called for each file listed on command line, and/or stdin
static void do_tail(int fd, char *name)
{
  long bytes = TT.c, lines = TT.n;
  int linepop = 1;

  if (FLAG(f)) {
    char *s = name;

    if (!fd) sprintf(s = toybuf, "/proc/self/fd/%d", fd);
    if (xnotify_add(TT.not, fd, s)) perror_exit("-f on '%s' failed", s);
  }

  if (TT.file_no++) xputc('\n');
  TT.last_fd = fd;
  if (toys.optc > 1) xprintf("==> %s <==\n", name);

  // Are we measuring from the end of the file?

  if (bytes<0 || lines<0) {
    struct line_list *list = 0, *new;

    // The slow codepath is always needed, and can handle all input,
    // so make lseek support optional.
    if (try_lseek(fd, bytes, lines)) return;

    // Read data until we run out, keep a trailing buffer
    for (;;) {
      // Read next page of data, appending to linked list in order
      if (!(new = read_chunk(fd, sizeof(toybuf)))) break;
      dlist_add_nomalloc((void *)&list, (void *)new);

      // If tracing bytes, add until we have enough, discarding overflow.
      if (TT.c) {
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
    llist_traverse(list, write_chunk);

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

  if (!FLAG(n) && !FLAG(c)) {
    char *arg = *args;

    // handle old "-42" style arguments
    if (arg && *arg == '-' && arg[1]) {
      TT.n = atolx(*(args++));
      toys.optc--;
    } else {
      // if nothing specified, default -n to -10
      TT.n = -10;
    }
  }

  if (FLAG(f)) TT.not = xnotify_init(toys.optc);
  loopfiles_rw(args, O_RDONLY|WARN_ONLY|(O_CLOEXEC*!FLAG(f)), 0, do_tail);

  if (FLAG(f) && TT.file_no) {
    for (;;) {
      char *path;
      int fd = xnotify_wait(TT.not, &path), len;

      // Read new data.
      while ((len = read(fd, toybuf, sizeof(toybuf)))>0) {
        if (TT.last_fd != fd) {
          TT.last_fd = fd;
          xprintf("\n==> %s <==\n", path);
        }

        xwrite(1, toybuf, len);
      }
    }
  }
}
