/* file.c - describe file type
 *
 * Copyright 2016 The Android Open Source Project
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/file.html

USE_FILE(NEWTOY(file, "<1hL[!hL]", TOYFLAG_USR|TOYFLAG_BIN))

config FILE
  bool "file"
  default y
  help
    usage: file [-hL] [file...]

    Examine the given files and describe their content types.

    -h	don't follow symlinks (default)
    -L	follow symlinks
*/

#define FOR_file
#include "toys.h"

GLOBALS(
  int max_name_len;
)

// We don't trust elf.h to be there, and two codepaths for 32/64 is awkward
// anyway, so calculate struct offsets manually. (It's a fixed ABI.)
static void do_elf_file(int fd, struct stat *sb)
{
  int endian = toybuf[5], bits = toybuf[4], i, j;
  int64_t (*elf_int)(void *ptr, unsigned size) = peek_le;
  // Values from include/linux/elf-em.h (plus arch/*/include/asm/elf.h)
  // Names are linux/arch/ directory (sometimes before 32/64 bit merges)
  struct {int val; char *name;} type[] = {{0x9026, "alpha"}, {93, "arc"},
    {195, "arcv2"}, {40, "arm"}, {183, "arm64"}, {0x18ad, "avr32"},
    {106, "blackfin"}, {140, "c6x"}, {23, "cell"}, {76, "cris"},
    {0x5441, "frv"}, {46, "h8300"}, {164, "hexagon"}, {50, "ia64"},
    {88, "m32r"}, {0x9041, "m32r"}, {4, "m68k"}, {174, "metag"},
    {0xbaab, "microblaze"}, {8, "mips"}, {10, "mips-old"}, {89, "mn10300"},
    {0xbeef, "mn10300-old"}, {113, "nios2"}, {92, "openrisc"},
    {0x8472, "openrisc-old"}, {15, "parisc"}, {20, "ppc"}, {21, "ppc64"},
    {22, "s390"}, {0xa390, "s390-old"}, {135, "score"}, {42, "sh"},
    {2, "sparc"}, {18, "sparc8+"}, {43, "sparc9"}, {188, "tile"},
    {191, "tilegx"}, {3, "386"}, {6, "486"}, {62, "x86-64"}, {94, "xtensa"},
    {0xabc7, "xtensa-old"}
  };
  int dynamic = 0;
  int stripped = 1;
  char *map;
  off_t phoff, shoff;
  int phentsize, phnum, shsize, shnum;

  printf("ELF ");

  // executable (ELF says this is short but reality says byte, not MSB swapped)
  i = toybuf[16];
  if (i == 1) printf("relocatable");
  else if (i == 2) printf("executable");
  else if (i == 3) printf("shared object");
  else if (i == 4) printf("core dump");
  else printf("(bad type %d)", i);
  printf(", ");

  // "64-bit"
  if (bits == 1) printf("32-bit ");
  else if (bits == 2) printf("64-bit ");
  else {
    printf("(bad class %d) ", bits);
    bits = 0;
  }

  // "LSB"
  if (endian == 1) printf("LSB ");
  else if (endian == 2) {
    printf("MSB ");
    elf_int = peek_be;
  } else {
    printf("(bad endian %d) \n", endian);
    endian = 0;
  }

  // e_machine, ala "x86", from big table above
  j = elf_int(toybuf+18, 2);
  for (i = 0; i<ARRAY_LEN(type); i++) if (j==type[i].val) break;
  if (i<ARRAY_LEN(type)) printf("%s", type[i].name);
  else printf("(unknown arch %d)", j);

  bits--;
  // If what we've seen so far doesn't seem consistent, bail.
  if (!((bits&1)==bits && endian)) {
    printf(", corrupt?\n");
    return;
  }

  // Stash what we need from the header; it's okay to reuse toybuf after this.
  phentsize = elf_int(toybuf+42+12*bits, 2);
  phnum = elf_int(toybuf+44+12*bits, 2);
  phoff = elf_int(toybuf+28+4*bits, 4+4*bits);
  shsize = elf_int(toybuf+46+12*bits, 2);
  shnum = elf_int(toybuf+48+12*bits, 2);
  shoff = elf_int(toybuf+32+8*bits, 4+4*bits);

  // With binutils, phentsize seems to only be non-zero if phnum is non-zero.
  // Such ELF files are rare, but do exist. (Android's crtbegin files, say.)
  if (phnum && (phentsize != 32+24*bits)) {
    printf(", corrupt phentsize %d?\n", phentsize);
    return;
  }

  map = xmmap(0, sb->st_size, PROT_READ, MAP_SHARED, fd, 0);

  // We need to read the phdrs for dynamic vs static.
  // (Note: fields got reordered for 64 bit)
  for (i = 0; i<phnum; i++) {
    char *phdr = map+phoff+i*phentsize;
    int p_type = elf_int(phdr, 4);
    long long p_offset, p_filesz;

    if (p_type==2 /*PT_DYNAMIC*/) dynamic = 1;
    if (p_type!=3 /*PT_INTERP*/ && p_type!=4 /*PT_NOTE*/) continue;

    j = bits+1;
    p_offset = elf_int(phdr+4*j, 4*j);
    p_filesz = elf_int(phdr+16*j, 4*j);

    if (p_type==3 /*PT_INTERP*/)
      printf(", dynamic (%.*s)", (int)p_filesz, map+p_offset);
  }
  if (!dynamic) printf(", static");

  // We need to read the shdrs for stripped/unstripped and any notes.
  // Notes are in program headers *and* section headers, but some files don't
  // contain program headers, so we prefer to check here.
  // (Note: fields got reordered for 64 bit)
  for (i = 0; i<shnum; i++) {
    char *shdr = map+shoff+i*shsize;
    int sh_type = elf_int(shdr+4, 4);
    long sh_offset = elf_int(shdr+8+8*(bits+1), 4*(bits+1));
    int sh_size = elf_int(shdr+8+12*(bits+1), 4);

    if (sh_type == 2 /*SHT_SYMTAB*/) {
      stripped = 0;
      break;
    } else if (sh_type == 7 /*SHT_NOTE*/) {
      char *note = map+sh_offset;

      // An ELF note is a sequence of entries, each consisting of an
      // ndhr followed by n_namesz+n_descsz bytes of data (each of those
      // rounded up to the next 4 bytes, without this being reflected in
      // the header byte counts themselves).
      while (sh_size >= 3*4) { // Don't try to read a truncated entry.
        int n_namesz = elf_int(note, 4);
        int n_descsz = elf_int(note+4, 4);
        int n_type = elf_int(note+8, 4);
        int notesz = 3*4 + ((n_namesz+3)&~3) + ((n_descsz+3)&~3);

        if (n_namesz==4 && !memcmp(note+12, "GNU", 4)) {
          if (n_type==3 /*NT_GNU_BUILD_ID*/) {
            printf(", BuildID=");
            for (j = 0; j < n_descsz; ++j) printf("%02x", note[16 + j]);
          }
        } else if (n_namesz==8 && !memcmp(note+12, "Android", 8)) {
          if (n_type==1 /*.android.note.ident*/) {
            printf(", for Android %d", (int)elf_int(note+20, 4));
            if (n_descsz > 24)
              printf(", built by NDK %.64s (%.64s)", note+24, note+24+64);
          }
        }

        note += notesz;
        sh_size -= notesz;
      }
    }
  }
  printf(", %sstripped", stripped ? "" : "not ");
  xputc('\n');

  munmap(map, sb->st_size);
}

static void do_regular_file(int fd, char *name, struct stat *sb)
{
  char *s;
  int len = read(fd, s = toybuf, sizeof(toybuf)-256);
  int magic;

  if (len<0) perror_msg("%s", name);

  if (len>40 && strstart(&s, "\177ELF")) do_elf_file(fd, sb);
  else if (len>=8 && strstart(&s, "!<arch>\n")) xprintf("ar archive\n");
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
  else if (len>32 && !memcmp(toybuf, "\xff\xd8", 2)) xputs("JPEG image data");

  // https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html
  else if (len>8 && strstart(&s, "\xca\xfe\xba\xbe"))
    xprintf("Java class file, version %d.%d\n",
      (int)peek_be(s+2, 2), (int)peek_be(s, 2));

  // https://people.freebsd.org/~kientzle/libarchive/man/cpio.5.txt
  // the lengths for cpio are size of header + 9 bytes, since any valid
  // cpio archive ends with a record for "TARGET!!!"
  else if (len>85 && strstart(&s, "07070")) {
    char *cpioformat = "unknown type";

    if (toybuf[5] == '7') cpioformat = "pre-SVR4 or odc";
    else if (toybuf[5] == '1') cpioformat = "SVR4 with no CRC";
    else if (toybuf[5] == '2') cpioformat = "SVR4 with CRC";
    xprintf("ASCII cpio archive (%s)\n", cpioformat);
  } else if (len>33 && (magic=peek(&s,2), magic==0143561 || magic==070707)) {
    if (magic == 0143561) printf("byte-swapped ");
    xprintf("cpio archive\n");
  // tar archive (ustar/pax or gnu)
  } else if (len>500 && !strncmp(s+257, "ustar", 5))
    xprintf("POSIX tar archive%s\n", strncmp(s+262,"  ",2)?"":" (GNU)");
  // zip/jar/apk archive, ODF/OOXML document, or such
  else if (len>5 && strstart(&s, "PK\03\04")) {
    int ver = toybuf[4];

    xprintf("Zip archive data");
    if (ver) xprintf(", requires at least v%d.%d to extract", ver/10, ver%10);
    xputc('\n');
  } else if (len>4 && strstart(&s, "BZh") && isdigit(*s))
    xprintf("bzip2 compressed data, block size = %c00k\n", *s);
  else if (len>10 && strstart(&s, "\x1f\x8b")) xputs("gzip compressed data");
  else {
    char *what = 0;
    int i, bytes;

    // If shell script, report which interpreter
    if (len>3 && strstart(&s, "#!")) {
      // Whitespace is allowed between the #! and the interpreter
      while (isspace(*s)) s++;
      if (strstart(&s, "/usr/bin/env")) while (isspace(*s)) s++;
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

void file_main(void)
{
  char **arg;

  for (arg = toys.optargs; *arg; ++arg) {
    int name_len = strlen(*arg);

    if (name_len > TT.max_name_len) TT.max_name_len = name_len;
  }

  // Can't use loopfiles here because it doesn't call function when can't open
  for (arg = toys.optargs; *arg; arg++) {
    char *name = *arg, *what = "cannot open";
    struct stat sb;
    int fd = !strcmp(name, "-");

    xprintf("%s: %*s", name, (int)(TT.max_name_len - strlen(name)), "");

    if (fd || !((toys.optflags & FLAG_L) ? stat : lstat)(name, &sb)) {
      if (fd || S_ISREG(sb.st_mode)) {
        if (!sb.st_size) what = "empty";
        else if ((fd = openro(name, O_RDONLY)) != -1) {
          do_regular_file(fd, name, &sb);
          if (fd) close(fd);
          continue;
        }
      } else if (S_ISFIFO(sb.st_mode)) what = "fifo";
      else if (S_ISBLK(sb.st_mode)) what = "block special";
      else if (S_ISCHR(sb.st_mode)) what = "character special";
      else if (S_ISDIR(sb.st_mode)) what = "directory";
      else if (S_ISSOCK(sb.st_mode)) what = "socket";
      else if (S_ISLNK(sb.st_mode)) what = "symbolic link";
      else what = "unknown";
    }

    xputs(what);
  }
}
