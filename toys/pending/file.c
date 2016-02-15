/* file.c - describe file type
 *
 * Copyright 2016 The Android Open Source Project
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/file.html

USE_FILE(NEWTOY(file, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config FILE
  bool "file"
  default n
  help
    usage: file [file...]

    Examine the given files and describe their content types.
*/

#define FOR_file
#include "toys.h"

GLOBALS(
  int max_name_len;
)

static void do_elf_file()
{
  int elf_endian = toybuf[5], e_type, e_machine, i;
  int64_t (*elf_int)(void *ptr, unsigned size) = peek_le;
  // Values from include/linux/elf-em.h (plus arch/*/include/asm/elf.h)
  // Names are linux/arch/ directory name
  struct {int val; char *name;} type[] = {{0x9026, "alpha"},
    {40, "arm"}, {183, "arm"}, {0x18ad, "avr32"}, {106, "blackfin"},
    {76, "cris"}, {0x5441, "frv"}, {46, "h8300"}, {50, "ia64"},//ia intel ftaghn
    {88, "m32r"}, {4, "m68k"}, {0xbaab, "microblaze"}, {8, "mips"},
    {10, "mips"}, {89, "mn10300"}, {15, "parisc"}, {22, "s390"},
    {135, "score"}, {42, "sh"}, {2, "sparc"}, {18, "sparc"}, {43, "sparc"},
    {187, "tile"}, {188, "tile"}, {191, "tile"}, {3, "x86"}, {6, "x86"},
    {62, "x86"}, {94, "xtensa"}, {0xabc7, "xtensa"}};

  xprintf("ELF ");

  // "64-bit"
  if (toybuf[4] == 1) xprintf("32-bit ");
  else if (toybuf[4] == 2) xprintf("64-bit ");
  else xprintf("(bad class %d)", toybuf[4]);

  // "LSB"
  if (elf_endian == 1) xprintf("LSB ");
  else if (elf_endian == 2) {
    xprintf("MSB ");
    elf_int = peek_be;
  } else {
    xprintf("(bad endian %d)\n", elf_endian);

    // At this point we can't parse remaining fields.
    return;
  }

  // ", executable"
  e_type = elf_int(&toybuf[0x10], 2);
  if (e_type == 1) xprintf("relocatable");
  else if (e_type == 2) xprintf("executable");
  else if (e_type == 3) xprintf("shared object");
  else if (e_type == 4) xprintf("core dump");
  else xprintf("(invalid type %d)", e_type);

  // ", x86-64"
  e_machine = elf_int(&toybuf[0x12], 2);
  for (i = 0; i<ARRAY_LEN(type); i++) if (e_machine == type[i].val) break;
  if (i<ARRAY_LEN(type)) xprintf(", %s", type[i].name);
  else xprintf(", (unknown arch %d)", e_machine);

  // "version 1"
  xprintf(", version %d", toybuf[6]);

  // " (SYSV)"
  // TODO: will we ever meet any of the others in practice?
  if (!toybuf[7]) xprintf(" (SYSV)");
  else xprintf(" (OS %d)", toybuf[7]);

  // TODO: we'd need to actually parse the ELF file to report the rest...
  // ", dynamically linked"
  // " (uses shared libs)"
  // ", for Linux 2.6.24"
  // ", BuildID[sha1]=SHA"
  // ", stripped"

  xputc('\n');
}

static void do_regular_file(int fd, char *name)
{
  char *s;
  int len = read(fd, s = toybuf, sizeof(toybuf)-256);

  if (len<0) perror_msg("%s", name);

  if (len>20 && strstart(&s, "\177ELF")) {
    do_elf_file(len);
  } else if (len>28 && strstart(&s, "\x89PNG\x0d\x0a\x1a\x0a")) {
    // PNG is big-endian: https://www.w3.org/TR/PNG/#7Integers-and-byte-order
    int chunk_length = peek_be(s, 4);

    xprintf("PNG image data");

    // The IHDR chunk comes first: https://www.w3.org/TR/PNG/#11IHDR
    s += 4;
    if (chunk_length == 13 && strstart(&s, "IHDR")) {
      // https://www.w3.org/TR/PNG/#6Colour-values
      char *c = 0, *colors[] = {"grayscale", 0, "color RGB", "indexed color",
                                "grayscale with alpha", 0, "color RGBA"};

      if (s[9]<ARRAY_LEN(colors)) c = colors[s[9]];
      if (!c) c = "unknown";

      xprintf(", %d x %d, %d-bit/%s, %sinterlaced", (int)peek_be(s, 4),
        (int)peek_be(s+4, 4), s[8], c, s[12] ? "" : "non-");
    }

    xputc('\n');

  // https://www.w3.org/Graphics/GIF/spec-gif89a.txt
  } else if (len>16 && (strstart(&s, "GIF87a") || strstart(&s, "GIF89a")))
    xprintf("GIF image data, %d x %d\n",
      (int)peek_le(s, 2), (int)peek_le(s+8, 2));

  // TODO: parsing JPEG for width/height is harder than GIF or PNG.
  else if (len>32 && memcmp(toybuf, "\xff\xd8", 2) == 0)
    xprintf("JPEG image data\n");

  // https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html
  else if (len>8 && strstart(&s, "\xca\xfe\xba\xbe"))
    xprintf("Java class file, version %d.%d\n",
      (int)peek_be(s+6, 2), (int)peek_be(s, 2));

    // TODO: cpio archive.
    // TODO: tar archive.
    // TODO: zip/jar/apk archive.
  else {
    char *what = 0;
    int i, bytes;

    // If shell script, report which interpreter
    if (len>3 && strstart(&s, "#!")) {
      for (what = s; (s-toybuf)<len && !isspace(*s); s++);
      strcpy(s, " script");

    // Distinguish ASCII text, UTF-8 text, or data
    } else for (i = 0; i<len; ++i) {
      if (!(isprint(toybuf[i]) || isspace(toybuf[i]))) {
        wchar_t wc;
        if ((bytes = mbrtowc(&wc, s+i, len-i, 0))>0 && wcwidth(wc)>=0) {
          i += bytes-1;
          if (!what) what = "UTF-8 text";
        } else {
          what = "data";
          break;
        }
      }
    }
    xputs(what ? what : "ASCII text");
  }
}

static void do_file(int fd, char *name)
{
  struct stat sb;
  char *what = "unknown";

  xprintf("%s: %*s", name, (int)(TT.max_name_len - strlen(name)), "");

  if (!fstat(fd, &sb)) what = "cannot open";
  if (S_ISREG(sb.st_mode)) {
    if (sb.st_size == 0) what = "empty";
    else {
      do_regular_file(fd, name);
      return;
    }
  } else if (S_ISBLK(sb.st_mode)) what = "block special";
  else if (S_ISCHR(sb.st_mode)) what = "character special";
  else if (S_ISDIR(sb.st_mode)) what = "directory";
  else if (S_ISFIFO(sb.st_mode)) what = "fifo";
  else if (S_ISSOCK(sb.st_mode)) what = "socket";
  else if (S_ISLNK(sb.st_mode)) what = "symbolic link";
  xputs(what);
}

void file_main(void)
{
  char **name;

  for (name = toys.optargs; *name; ++name) {
    int name_len = strlen(*name);

    if (name_len > TT.max_name_len) TT.max_name_len = name_len;
  }

  loopfiles(toys.optargs, do_file);
}
