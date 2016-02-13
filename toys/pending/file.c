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

// TODO: all the ELF magic numbers are available in <elf.h> --- use that?

static char *elf_arch(int e_machine)
{
  // TODO: include obsolete stuff we'll never see, like "PDP-10" and "VAX"?
  switch (e_machine) {
  case 3: return "Intel 80386";
  case 8: return "MIPS";
  case 40: return "ARM";
  case 62: return "x86-64";
  case 183: return "ARM aarch64";
  default: return NULL;
  }
}

static int64_t elf_int(int endian, char *data, int bytes)
{
  if (endian == 1) return peek_le(data, bytes);
  return peek_be(data, bytes);
}

static void do_elf_file()
{
  int elf_endian = toybuf[5];
  int e_type, e_machine;

  xprintf("ELF");

  // "64-bit"
  if (toybuf[4] == 1) xprintf(" 32-bit");
  else if (toybuf[4] == 2) xprintf(" 64-bit");
  else xprintf(" (invalid class %d)", toybuf[4]);

  // "LSB"
  if (elf_endian == 1) xprintf(" LSB");
  else if (elf_endian == 2) xprintf(" MSB");
  else xprintf("(invalid endian %d) ", elf_endian);

  if (elf_endian == 1 || elf_endian == 2) {
    char *arch;

    // ", executable"
    e_type = elf_int(elf_endian, &toybuf[0x10], 2);
    if (e_type == 1) xprintf(" relocatable");
    else if (e_type == 2) xprintf(" executable");
    else if (e_type == 3) xprintf(" shared object");
    else if (e_type == 4) xprintf(" core dump");
    else xprintf(" (invalid type %d)", e_type);

    // ", x86-64"
    e_machine = elf_int(elf_endian, &toybuf[0x12], 2);
    arch = elf_arch(e_machine);
    if (arch) xprintf(", %s", arch);
    else xprintf(", (unknown arch %d)", e_machine);
  }

  // "version 1"
  xprintf(", version %d", toybuf[6]);

  // " (SYSV)"
  // TODO: will we ever meet any of the others in practice?
  if (toybuf[7] == 0) xprintf(" (SYSV)");
  else xprintf(" (OS %d)", toybuf[7]);

  // TODO: we'd need to actually parse the ELF file to report the rest...
  // ", dynamically linked"
  // " (uses shared libs)"
  // ", for GNU/Linux 2.6.24"
  // ", BuildID[sha1]=SHA"
  // ", stripped"

  xputs("");
}

// https://www.w3.org/TR/PNG/#6Colour-values
static char *png_color_type(int color_type)
{
  switch (color_type) {
  case 0: return "grayscale";
  case 2: return "color RGB";
  case 3: return "indexed color";
  case 4: return "grayscale with alpha";
  case 6: return "color RGBA";
  default: return "unknown";
  }
}

static void do_png_file()
{
  // PNG is big-endian: https://www.w3.org/TR/PNG/#7Integers-and-byte-order
  int chunk_length = peek_be(&toybuf[8], 4);

  xprintf("PNG image data");

  // The IHDR chunk comes first.
  // https://www.w3.org/TR/PNG/#11IHDR
  if (chunk_length == 13 && memcmp(&toybuf[12], "IHDR", 4) == 0) {
    int width = peek_be(&toybuf[16], 4);
    int height = peek_be(&toybuf[20], 4);
    int bits = toybuf[24] & 0xff;
    int type = toybuf[25] & 0xff;
    int interlaced = toybuf[28] & 0xff;

    xprintf(", %d x %d, %d-bit/%s, %s", width, height, bits,
            png_color_type(type),
            interlaced ? "interlaced" : "non-interlaced");
  }

  xputs("");
}

static void do_gif_file()
{
  // https://www.w3.org/Graphics/GIF/spec-gif89a.txt
  int width = peek_le(&toybuf[6], 2);
  int height = peek_le(&toybuf[8], 2);

  xprintf("GIF image data, %d x %d\n", width, height);
}

static void do_jpeg_file()
{
  // TODO: parsing JPEG for width/height is harder than GIF or PNG.
  xprintf("JPEG image data\n");
}

static void do_java_class_file()
{
  // https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html
  int minor = peek_be(&toybuf[4], 2);
  int major = peek_be(&toybuf[6], 2);

  xprintf("Java class file, version %d.%d\n", major, minor);
}

static void do_regular_file(int fd, char *name)
{
  int len = read(fd, toybuf, sizeof(toybuf));

  if (len<0) perror_msg("cannot open");

  if (len>20 && memcmp(toybuf, "\177ELF", 4) == 0) {
    do_elf_file(len);
  } else if (len>28 && memcmp(toybuf, "\x89PNG\x0d\x0a\x1a\x0a", 8) == 0) {
    do_png_file();
  } else if (len>16 && (memcmp(toybuf, "GIF87a", 6) == 0 ||
                        memcmp(toybuf, "GIF89a", 6) == 0)) {
    do_gif_file();
  } else if (len>32 && memcmp(toybuf, "\xff\xd8", 2) == 0) {
    do_jpeg_file();
  } else if (len>8 && memcmp(toybuf, "\xca\xfe\xba\xbe", 4) == 0) {
    do_java_class_file();

    // TODO: cpio archive.
    // TODO: tar archive.
    // TODO: zip/jar/apk archive.
  } else {
    char *what = "ASCII text";
    int i;

    // TODO: report which interpreter?
    if (strncmp(toybuf, "#!", 2) == 0) what = "commands text";

    // TODO: try UTF-8 too before falling back to "data".
    for (i = 0; i < len; ++i) {
      if (!(isprint(toybuf[i]) || isspace(toybuf[i]))) {
        what = "data";
        break;
      }
    }
    xputs(what);
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

static void init_max_name_len()
{
  char **name;
  int name_len;

  for (name = toys.optargs; *name; ++name) {
    name_len = strlen(*name);
    if (name_len > TT.max_name_len) TT.max_name_len = name_len;
  }
}

void file_main(void)
{
  init_max_name_len();
  loopfiles(toys.optargs, do_file);
}
