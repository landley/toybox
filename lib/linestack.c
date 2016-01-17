#include "toys.h"

// Insert one stack into another before position in old stack.
// (Does not copy contents of strings, just shuffles index array contents.)
void linestack_addstack(struct linestack **lls, struct linestack *throw,
  long pos)
{
  struct linestack *catch = *lls;

  if (CFG_TOYBOX_DEBUG)
    if (pos > catch->len) error_exit("linestack_addstack past end.");

  // Make a hole, allocating more space if necessary.
  if (catch->len+throw->len >= catch->max) {
    // New size rounded up to next multiple of 64, allocate and copy start.
    catch->max = ((catch->len+throw->len)|63)+1;
    *lls = xmalloc(sizeof(struct linestack)+catch->max*sizeof(struct ptr_len));
    memcpy(*lls, catch, sizeof(struct linestack)+pos*sizeof(struct ptr_len));
  }

  // Copy end (into new allocation if necessary)
  if (pos != catch->len)
    memmove((*lls)->idx+pos+throw->len, catch->idx+pos,
      (catch->len-pos)*sizeof(struct ptr_len));

  // Cleanup if we had to realloc.
  if (catch != *lls) {
    free(catch);
    catch = *lls;
  }

  memcpy(catch->idx+pos, throw->idx, throw->len*sizeof(struct ptr_len));
  catch->len += throw->len;
}

void linestack_insert(struct linestack **lls, long pos, char *line, long len)
{
  // alloca() was in 32V and Turbo C for DOS, but isn't in posix or c99.
  // I'm not thrashing the heap for this, but this should work even if
  // a broken compiler adds gratuitous padding.
  struct {
    struct linestack ls;
    struct ptr_len pl;
  } ls;

  ls.ls.len = ls.ls.max = 1;
  ls.ls.idx[0].ptr = line;
  ls.ls.idx[0].len = len;
  linestack_addstack(lls, &ls.ls, pos);
}

void linestack_append(struct linestack **lls, char *line)
{
  linestack_insert(lls, (*lls)->len, line, strlen(line));
}

struct linestack *linestack_load(char *name)
{
  FILE *fp = fopen(name, "r");
  struct linestack *ls;

  if (!fp) return 0;

  ls = xzalloc(sizeof(struct linestack));

  for (;;) {
    char *line = 0;
    ssize_t len;

    if ((len = getline(&line, (void *)&len, fp))<1) break;
    if (line[len-1]=='\n') len--;
    linestack_insert(&ls, ls->len, line, len);
  }
  fclose(fp);

  return ls;
}

// Show width many columns, negative means from right edge.
// If out=0 just measure
// if escout, send it unprintable chars, returns columns output or -1 for
// standard escape: ^X if <32, <XX> if invliad UTF8, U+XXXX if UTF8 !iswprint()
// Returns width in columns, moves *str to end of data consumed.
int crunch_str(char **str, int width, FILE *out,
  int (*escout)(FILE *out, int cols, char **buf))
{
  int columns = 0, col, bytes;
  char *start, *end;

  for (end = start = *str; *end;) {
    wchar_t wc = *end;

    bytes = 0;
    if (*end >= ' ' && (bytes = mbrtowc(&wc, end, 99,0))>0
        && (col = wcwidth(wc))>=0)
    {
      if (width-columns<col) break;
      if (out) fwrite(end, bytes, 1, out);
    } else if (!escout || 0>(col = escout(out, width-columns, &end))) {
      char buf[32];

      tty_esc("7m");
      if (*end < ' ') {
        bytes = 1;
        sprintf(buf, "^%c", '@'+*end);
      } else if (bytes<1) {
        bytes = 1;
        sprintf(buf, "<%02X>", *end);
      } else sprintf(buf, "U+%04X", wc);
      col = strlen(buf);
      if (width-columns<col) buf[col = width-columns] = 0;
      if (out) fputs(buf, out);
      tty_esc("27m");
    } else continue;
    columns += col;
    end += bytes;
  }
  *str = end;

  return columns;
}

// Write width chars at start of string to strdout with standard escapes
// Returns length in columns so caller can pad it out with spaces.
int draw_str(char *start, int width)
{
  return crunch_str(&start, width, stdout, 0);
}

// Return utf8 columns
int utf8len(char *str)
{
  return crunch_str(&str, INT_MAX, 0, 0);
}

// Return bytes used by (up to) this many columns
int utf8skip(char *str, int width)
{
  char *s = str;

  crunch_str(&s, width, 0, 0);

  return s-str;
}

// Print utf8 to stdout with standard escapes,trimmed to width and padded
// out to padto. If padto<0 left justify. Returns columns printed
int draw_trim(char *str, int padto, int width)
{
  int apad = abs(padto), len = utf8len(str);

  if (padto<0 && len>width) str += utf8skip(str, len-width);
  if (len>width) len = width;

  // Left pad if right justified 
  if (padto>0 && apad>len) printf("%*s", apad-len, "");
  crunch_str(&str, len, stdout, 0);
  if (padto<0 && apad>len) printf("%*s", apad-len, "");

  return (apad > len) ? apad : len;
}
