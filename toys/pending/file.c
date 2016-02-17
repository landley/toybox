/* file.c - describe file type
 *
 * Copyright 2016 The Android Open Source Project
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/file.html
 *
 * TODO: ar

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

// We don't trust elf.h to be there, and two codepaths for 32/64 is awkward
// anyway, so calculate struct offsets manually. (It's a fixed ABI.)
static void do_elf_file(int fd)
{
  int endian = toybuf[5], bits = toybuf[4], i, j;
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
  if (bits == 1) xprintf("32-bit ");
  else if (bits == 2) xprintf("64-bit ");
  else {
    xprintf("(bad class %d) ", bits);
    bits = 0;
  }

  // e_machine, ala "x86", from big table above
  j = elf_int(toybuf+18, 2);
  for (i = 0; i<ARRAY_LEN(type); i++) if (j==type[i].val) break;
  if (i<ARRAY_LEN(type)) xprintf("%s ", type[i].name);
  else xprintf("(unknown arch %d) ", j);

  // "LSB"
  if (endian == 1) xprintf("LSB ");
  else if (endian == 2) {
    xprintf("MSB ");
    elf_int = peek_be;
  } else {
    xprintf("(bad endian %d)\n", endian);
    endian = 0;
  }

  // ", executable"
  i = elf_int(toybuf+16, 2);
  if (i == 1) xprintf("relocatable");
  else if (i == 2) xprintf("executable");
  else if (i == 3) xprintf("shared object");
  else if (i == 4) xprintf("core dump");
  else xprintf("(bad type %d)", i);

  bits--;
  // If we know our bits and endianness and phentsize agrees show dynamic linker
  if ((bits&1)==bits && endian &&
      (i = elf_int(toybuf+42+12*bits, 2)) == 32+24*bits)
  {
    char *map, *phdr;
    int phsize = i, phnum = elf_int(toybuf+44+12*bits, 2),
        psz = sysconf(_SC_PAGE_SIZE), lib = 0;
    off_t phoff = elf_int(toybuf+28+4*bits, 4+4*bits),
          mapoff = phoff^(phoff&(psz-1));

    // map e_phentsize*e_phnum bytes at e_phoff
    map = mmap(0, phsize*phnum, PROT_READ, MAP_SHARED, fd, mapoff);
    if (map) {
      // Find PT_INTERP entry. (Not: fields got reordered for 64 bit)
      for (i = 0; i<phnum; i++) {
        long long dlpos, dllen;

        // skip non-PT_INTERP entries
        j = elf_int(phdr = map+(phoff-mapoff)+i*phsize, 4);
        if (j==2) lib++;
        if (j!=3) continue;

        // Read p_offset and p_filesz
        j = bits+1;
        dlpos = elf_int(phdr+4*j, 4*j);
        dllen = elf_int(phdr+16*j, 4*j);
        if (dllen<0 || dllen>sizeof(toybuf)-128
            || dlpos!=lseek(fd, dlpos, SEEK_SET)
            || dllen!=readall(fd, toybuf+128, dllen)) break;
        printf(", dynamic (%.*s)", (int)dllen, toybuf+128);
      }
      if (!lib) printf(", static");
      else printf(", needs %d lib%s", lib, lib>1 ? "s" : "");
      munmap(map, phsize*phnum);
    }
  }

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

  if (len>40 && strstart(&s, "\177ELF")) do_elf_file(fd);
  else if (len>28 && strstart(&s, "\x89PNG\x0d\x0a\x1a\x0a")) {
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
