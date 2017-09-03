#include "toys.h"

// A linestack is an array of struct ptr_len.

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

  // Copy new chunk we made space for
  memcpy(catch->idx+pos, throw->idx, throw->len*sizeof(struct ptr_len));
  catch->len += throw->len;
}

// Insert one line/len into a linestack at pos
void linestack_insert(struct linestack **lls, long pos, char *line, long len)
{
  // alloca() was in 32V and Turbo C for DOS, but isn't in posix or c99.
  // This allocates enough memory for the linestack to have one ptr_len.
  // (Even if a compiler adds gratuitous padidng that just makes it bigger.)
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

// Show width many columns, negative means from right edge, out=0 just measure
// if escout, send it unprintable chars, otherwise pass through raw data.
// Returns width in columns, moves *str to end of data consumed.
int crunch_str(char **str, int width, FILE *out, char *escmore,
  int (*escout)(FILE *out, int cols, int wc))
{
  int columns = 0, col, bytes;
  char *start, *end;

  for (end = start = *str; *end; columns += col, end += bytes) {
    wchar_t wc;

    if ((bytes = utf8towc(&wc, end, 4))>0 && (col = wcwidth(wc))>=0)
    {
      if (!escmore || wc>255 || !strchr(escmore, wc)) {
        if (width-columns<col) break;
        if (out) fwrite(end, bytes, 1, out);

        continue;
      }
    }

    if (bytes<1) {
      bytes = 1;
      wc = *end;
    }
    col = width-columns;
    if (col<1) break;
    if (escout) col = escout(out, col, wc);
    else if (out) fwrite(end, bytes, 1, out);
  }
  *str = end;

  return columns;
}


// standard escapes: ^X if <32, <XX> if invliad UTF8, U+XXXX if UTF8 !iswprint()
int crunch_escape(FILE *out, int cols, int wc)
{
  char buf[8];
  int rc;

  if (wc<' ') rc = sprintf(buf, "^%c", '@'+wc);
  else if (wc<256) rc = sprintf(buf, "<%02X>", wc);
  else rc = sprintf(buf, "U+%04X", wc);

  if (rc > cols) buf[rc = cols] = 0;
  if (out) fputs(buf, out);

  return rc;
}

// Display "standard" escapes in reverse video.
int crunch_rev_escape(FILE *out, int cols, int wc)
{
  int rc;

  tty_esc("7m");
  rc = crunch_escape(out, cols, wc);
  tty_esc("27m");

  return rc;
}

// Write width chars at start of string to strdout with standard escapes
// Returns length in columns so caller can pad it out with spaces.
int draw_str(char *start, int width)
{
  return crunch_str(&start, width, stdout, 0, crunch_rev_escape);
}

// Return utf8 columns
int utf8len(char *str)
{
  return crunch_str(&str, INT_MAX, 0, 0, crunch_rev_escape);
}

// Return bytes used by (up to) this many columns
int utf8skip(char *str, int width)
{
  char *s = str;

  crunch_str(&s, width, 0, 0, crunch_rev_escape);

  return s-str;
}

// Print utf8 to stdout with standard escapes, trimmed to width and padded
// out to padto. If padto<0 left justify. Returns columns printed
int draw_trim_esc(char *str, int padto, int width, char *escmore,
  int (*escout)(FILE *out, int cols, int wc))
{
  int apad = abs(padto), len = utf8len(str);

  if (padto>=0 && len>width) str += utf8skip(str, len-width);
  if (len>width) len = width;

  // Left pad if right justified 
  if (padto>0 && apad>len) printf("%*s", apad-len, "");
  crunch_str(&str, len, stdout, 0, crunch_rev_escape);
  if (padto<0 && apad>len) printf("%*s", apad-len, "");

  return (apad > len) ? apad : len;
}

// draw_trim_esc() with default escape
int draw_trim(char *str, int padto, int width)
{
  return draw_trim_esc(str, padto, width, 0, 0);
}
