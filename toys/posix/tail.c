/* tail.c - copy last lines from input to stdout.
 *
 * Copyright 2012 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/tail.html

USE_TAIL(NEWTOY(tail, "fc-n-", TOYFLAG_BIN))

config TAIL
  bool "tail"
  default y
  help
    usage: tail [-n|c number] [-f] [file...]

    Copy last lines from files to stdout. If no files listed, copy from
    stdin. Filename "-" is a synonym for stdin.

    -n	output the last X lines (default 10), +X counts from start.
    -c	output the last X bytes, +X counts from start
    -f	follow file, waiting for more data to be appended

config TAIL_SEEK
  bool "tail seek support"
  default y
  depends on TAIL
  help
    This version uses lseek, which is faster on large files.
*/

#define FOR_tail
#include "toys.h"

GLOBALS(
  long lines;
  long bytes;

  int file_no;
)

struct line_list {
  struct line_list *next;
  char *data;
  size_t len;
};

static struct line_list *get_chunk(int fd, size_t len)
{
  struct line_list *line = xmalloc(sizeof(struct line_list)+len);

  line->data = (char*)line + sizeof(struct line_list);
  line->len = readall(fd, line->data, len);
  line->next = 0;

  if (line->len + 1 < 2) {
    free(line);
    return 0;
  }

  return line;
}

static void dump_chunk(void *ptr)
{
  struct line_list *list = ptr;
  size_t len = list->len - (list->data - (char*)list - sizeof(struct line_list));

  xwrite(1, list->data, len);
  free(list);
}

// Reading through very large files is slow.  Using lseek can speed things
// up a lot, but isn't applicable to all input (cat | tail).
// Note: bytes and lines are negative here.
static int try_lseek(int fd, long bytes, long lines)
{
  struct line_list *list = 0, *temp;
  int flag = 0;
  size_t chunk = sizeof(toybuf), pos = lseek(fd, 0, SEEK_END);

  // If lseek() doesn't work on this stream, return now.
  if (pos == -1) return 0;

  // Seek to the right spot, output data from there.
  if (bytes) {
    if (lseek(fd, bytes, SEEK_END)<0) lseek(fd, 0, SEEK_SET);
    xsendfile(fd, 1);
    return 1;
  }

  // Read from end to find enough lines, then output them.

  bytes = pos;
  while (lines && pos) {
    size_t offset;

    // Read in next chunk from end of file
    if (chunk > pos) chunk = pos;
    pos -= chunk;
    if (pos != lseek(fd, pos, SEEK_SET)) {
      perror_msg("seek failed");
      break;
    }
    if (!(temp = get_chunk(fd, chunk))) break;
    if (list) temp->next = list;
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

        goto done;
      }
    }
  }

done:
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

  if (toys.optc > 1) {
    if (TT.file_no++) xputc('\n');
    xprintf("==> %s <==\n", name);
  }

  // Are we measuring from the end of the file?

  if (bytes<0 || lines<0) {
    struct line_list *head = 0, *tail, *new;
    // circular buffer of lines
    struct {
      char *start;
      struct line_list *inchunk;
    } *l = xzalloc(2*-lines*sizeof(void*));
    int i = 0, flag = 0;
    size_t count, len = bytes;

    // The slow codepath is always needed, and can handle all input,
    // so make lseek support optional.
    if (CFG_TAIL_SEEK && try_lseek(fd, bytes, lines)) return;

    // Read data until we run out, keep a trailing buffer
    for (;;) {
      char *try;

      if (!(new = get_chunk(fd, sizeof(toybuf)))) break;
      // append in order
      if (head) tail->next = new;
      else head = new;
      tail = new;

      try = new->data;
      if (lines) for (count = 0; count < new->len; count++, try++) {
        if (flag) { // last char was a newline
            while (l[i].inchunk && (l[i].inchunk!=head)) free(llist_pop(&head));
            l[i].inchunk = tail;
            l[i].start = try;
            i = (i + 1) % -lines;
            flag = 0;
        }
        if (*try == '\n') flag = 1;
      } else { // bytes
        if (len + new->len < len) flag = 1; // overflow -> have now read enough
        for (len += new->len; flag && (len - head->len < len);) {
          len -= head->len;
          free(llist_pop(&head));
        }
      }
    }
    if (lines) head->data = l[i].start;
    else head->data += len;

    // Output/free the buffer.
    llist_traverse(head, dump_chunk);

    free(l);
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

  // -f support: cache name/descriptor
}

void tail_main(void)
{
  // if nothing specified, default -n to -10
  if (!(toys.optflags&(FLAG_n|FLAG_c))) TT.lines = -10;

  loopfiles(toys.optargs, do_tail);

  // do -f stuff
}
