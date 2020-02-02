/* file.c - describe file type
 *
 * Copyright 2016 The Android Open Source Project
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/file.html

USE_FILE(NEWTOY(file, "<1bhLs[!hL]", TOYFLAG_USR|TOYFLAG_BIN))

config FILE
  bool "file"
  default y
  help
    usage: file [-bhLs] [FILE...]

    Examine the given files and describe their content types.

    -b	Brief (no filename)
    -h	Don't follow symlinks (default)
    -L	Follow symlinks
    -s	Show block/char device contents
*/

#define FOR_file
#include "toys.h"

GLOBALS(
  int max_name_len;

  off_t len;
)

// We don't trust elf.h to be there, and two codepaths for 32/64 is awkward
// anyway, so calculate struct offsets manually. (It's a fixed ABI.)
static void do_elf_file(int fd)
{
  int endian = toybuf[5], bits = toybuf[4], i, j, dynamic = 0, stripped = 1,
      phentsize, phnum, shsize, shnum;
  int64_t (*elf_int)(void *ptr, unsigned size);
  char *map = 0;
  off_t phoff, shoff;

  printf("ELF ");
  elf_int = (endian==2) ? peek_be : peek_le;

  // executable type
  i = elf_int(toybuf+16, 2);
  if (i == 1) printf("relocatable");
  else if (i == 2) printf("executable");
  else if (i == 3) printf("shared object");
  else if (i == 4) printf("core dump");
  else printf("(bad type %d)", i);
  if (elf_int(toybuf+36+12*(bits==2), 4) & 0x8000) printf(" (fdpic)");
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
  else if (endian == 2) printf("MSB ");
  else {
    printf("(bad endian %d) \n", endian);
    endian = 0;
  }

  // "x86".
  printf("%s", elf_arch_name(elf_int(toybuf+18, 2)));

  bits--;
  // If what we've seen so far doesn't seem consistent, bail.
  if (!((bits&1)==bits && endian)) {
    printf(", corrupt?\n");
    return;
  }

  // Parsing ELF means following tables that may point to data earlier in
  // the file, so sequential reading involves buffering unknown amounts of
  // data. Just skip it if we can't mmap.
  if (MAP_FAILED == (map = mmap(0, TT.len, PROT_READ, MAP_SHARED, fd, 0)))
    goto bad;

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
    printf(", corrupt phentsize %d?", phentsize);
    goto bad;
  }

  // Parsing ELF means following tables that may point to data earlier in
  // the file, so sequential reading involves buffering unknown amounts of
  // data. Just skip it if we can't mmap.
  if (MAP_FAILED == (map = mmap(0, TT.len, PROT_READ, MAP_SHARED, fd, 0)))
    goto bad;

  // We need to read the phdrs for dynamic vs static.
  // (Note: fields got reordered for 64 bit)
  if (phoff+phnum*phentsize>TT.len) goto bad;
  for (i = 0; i<phnum; i++) {
    char *phdr = map+phoff+i*phentsize;
    int p_type = elf_int(phdr, 4);
    long long p_offset, p_filesz;

    if (p_type==2 /*PT_DYNAMIC*/) dynamic = 1;
    if (p_type!=3 /*PT_INTERP*/ && p_type!=4 /*PT_NOTE*/) continue;

    j = bits+1;
    p_offset = elf_int(phdr+4*j, 4*j);
    p_filesz = elf_int(phdr+16*j, 4*j);

    if (p_type==3 /*PT_INTERP*/) {
      if (p_offset+p_filesz>TT.len) goto bad;
      printf(", dynamic (%.*s)", (int)p_filesz, map+p_offset);
    }
  }
  if (!dynamic) printf(", static");

  // We need to read the shdrs for stripped/unstripped and any notes.
  // Notes are in program headers *and* section headers, but some files don't
  // contain program headers, so we prefer to check here.
  // (Note: fields got reordered for 64 bit)
  if (shoff+i*shnum>TT.len) goto bad;
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
        unsigned n_namesz, n_descsz, n_type, notesz;

        if (sh_offset+sh_size>TT.len) goto bad;

        n_namesz = elf_int(note, 4);
        n_descsz = elf_int(note+4, 4);
        n_type = elf_int(note+8, 4);
        notesz = 3*4 + ((n_namesz+3)&~3) + ((n_descsz+3)&~3);

        // Does the claimed size of this note actually fit in the section?
        if (notesz > sh_size) goto bad;

        if (n_namesz==4 && !memcmp(note+12, "GNU", 4)) {
          if (n_type==3 /*NT_GNU_BUILD_ID*/) {
            printf(", BuildID=");
            for (j = 0; j < n_descsz; ++j) printf("%02x", note[16 + j]);
          }
        } else if (n_namesz==8 && !memcmp(note+12, "Android", 8)) {
          if (n_type==1 /*.android.note.ident*/ && n_descsz >= 4) {
            printf(", for Android %d", (int)elf_int(note+20, 4));
            // NDK r14 and later also include NDK version info. OS binaries
            // and binaries built by older NDKs don't have this.
            if (n_descsz >= 4+64+64)
              printf(", built by NDK %.64s (%.64s)", note+24, note+24+64);
          }
        }

        note += notesz;
        sh_size -= notesz;
      }
    }
  }
  printf(", %sstripped", stripped ? "" : "not ");
bad:
  xputc('\n');

  if (map && map != MAP_FAILED) munmap(map, TT.len);
}

static void do_regular_file(int fd, char *name)
{
  char *s;
  int len, magic;

  // zero through elf shnum, just in case
  memset(toybuf, 0, 80);
  if ((len = readall(fd, s = toybuf, sizeof(toybuf)))<0) perror_msg("%s", name);

  if (!len) xputs("empty");
  // 45 bytes: https://www.muppetlabs.com/~breadbox/software/tiny/teensy.html
  else if (len>=45 && strstart(&s, "\177ELF")) do_elf_file(fd);
  else if (len>=8 && strstart(&s, "!<arch>\n")) xputs("ar archive");
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

  // https://en.wikipedia.org/wiki/Java_class_file#General_layout
  else if (len>8 && strstart(&s, "\xca\xfe\xba\xbe"))
    xprintf("Java class file, version %d.%d (Java 1.%d)\n",
      (int)peek_be(s+2, 2), (int)peek_be(s, 2), (int)peek_be(s+2, 2)-44);

  // https://source.android.com/devices/tech/dalvik/dex-format#dex-file-magic
  else if (len>8 && strstart(&s, "dex\n") && s[3] == 0)
    xprintf("Android dex file, version %s\n", s);

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
    xputs("cpio archive");
  // tar archive (old, ustar/pax, or gnu)
  } else if (len>500 && is_tar_header(s))
    xprintf("%s tar archive%s\n", s[257] ? "POSIX" : "old",
      strncmp(s+262,"  ",2)?"":" (GNU)");
  // zip/jar/apk archive, ODF/OOXML document, or such
  else if (len>5 && strstart(&s, "PK\03\04")) {
    int ver = toybuf[4];

    xprintf("Zip archive data");
    if (ver) xprintf(", requires at least v%d.%d to extract", ver/10, ver%10);
    xputc('\n');
  } else if (len>4 && strstart(&s, "BZh") && isdigit(*s))
    xprintf("bzip2 compressed data, block size = %c00k\n", *s);
  else if (len > 31 && peek_be(s, 7) == 0xfd377a585a0000UL)
    xputs("xz compressed data");
  else if (len>10 && strstart(&s, "\x1f\x8b")) xputs("gzip compressed data");
  else if (len>32 && !memcmp(s+1, "\xfa\xed\xfe", 3)) {
    int bit = s[0]=='\xce'?32:64;
    char *what;

    xprintf("Mach-O %d-bit ", bit);

    if (s[4] == 7) what = (bit==32)?"x86":"x86-";
    else if (s[4] == 12) what = "arm";
    else if (s[4] == 18) what = "ppc";
    else what = NULL;
    if (what) xprintf("%s%s ", what, (bit==32)?"":"64");
    else xprintf("(bad arch %d) ", s[4]);

    if (s[12] == 1) what = "object";
    else if (s[12] == 2) what = "executable";
    else if (s[12] == 6) what = "shared library";
    else what = NULL;
    if (what) xprintf("%s\n", what);
    else xprintf("(bad type %d)\n", s[9]);
  } else if (len>36 && !memcmp(s, "OggS\x00\x02", 6)) {
    xprintf("Ogg data");
    // https://wiki.xiph.org/MIMETypesCodecs
    if (!memcmp(s+28, "CELT    ", 8)) xprintf(", celt audio");
    else if (!memcmp(s+28, "CMML    ", 8)) xprintf(", cmml text");
    else if (!memcmp(s+28, "BBCD\0", 5)) xprintf(", dirac video");
    else if (!memcmp(s+28, "\177FLAC", 5)) xprintf(", flac audio");
    else if (!memcmp(s+28, "\x8bJNG\r\n\x1a\n", 8)) xprintf(", jng video");
    else if (!memcmp(s+28, "\x80kate\0\0\0", 8)) xprintf(", kate text");
    else if (!memcmp(s+28, "OggMIDI\0", 8)) xprintf(", midi text");
    else if (!memcmp(s+28, "\x8aMNG\r\n\x1a\n", 8)) xprintf(", mng video");
    else if (!memcmp(s+28, "OpusHead", 8)) xprintf(", opus audio");
    else if (!memcmp(s+28, "PCM     ", 8)) xprintf(", pcm audio");
    else if (!memcmp(s+28, "\x89PNG\r\n\x1a\n", 8)) xprintf(", png video");
    else if (!memcmp(s+28, "Speex   ", 8)) xprintf(", speex audio");
    else if (!memcmp(s+28, "\x80theora", 7)) xprintf(", theora video");
    else if (!memcmp(s+28, "\x01vorbis", 7)) xprintf(", vorbis audio");
    else if (!memcmp(s+28, "YUV4MPEG", 8)) xprintf(", yuv4mpeg video");
    xputc('\n');
  } else if (len>32 && !memcmp(s, "RIF", 3) && !memcmp(s+8, "WAVEfmt ", 8)) {
    // https://en.wikipedia.org/wiki/WAV
    int le = (s[3] == 'F');
    int format = le ? peek_le(s+20,2) : peek_be(s+20,2);
    int channels = le ? peek_le(s+22,2) : peek_be(s+22,2);
    int hz = le ? peek_le(s+24,4) : peek_be(s+24,4);
    int bits = le ? peek_le(s+34,2) : peek_be(s+34,2);

    xprintf("WAV audio, %s, ", le ? "LE" : "BE");
    if (bits != 0) xprintf("%d-bit, ", bits);
    if (channels==1||channels==2) xprintf("%s, ", channels==1?"mono":"stereo");
    else xprintf("%d-channel, ", channels);
    xprintf("%d Hz, ", hz);
    // See https://tools.ietf.org/html/rfc2361, though there appear to be bugs
    // in the RFC. This assumes wikipedia's example files are more correct.
    if (format == 0x01) xprintf("PCM");
    else if (format == 0x03) xprintf("IEEE float");
    else if (format == 0x06) xprintf("A-law");
    else if (format == 0x07) xprintf("µ-law");
    else if (format == 0x11) xprintf("ADPCM");
    else if (format == 0x22) xprintf("Truespeech");
    else if (format == 0x31) xprintf("GSM");
    else if (format == 0x55) xprintf("MP3");
    else if (format == 0x70) xprintf("CELP");
    else if (format == 0xfffe) xprintf("extensible");
    else xprintf("unknown format %d", format);
    xputc('\n');
  } else if (len>12 && !memcmp(s, "\x00\x01\x00\x00", 4)) {
    xputs("TrueType font");
  } else if (len>12 && !memcmp(s, "ttcf\x00", 5)) {
    xprintf("TrueType font collection, version %d, %d fonts\n",
            (int)peek_be(s+4, 2), (int)peek_be(s+8, 4));

  // https://docs.microsoft.com/en-us/typography/opentype/spec/otff
  } else if (len>12 && !memcmp(s, "OTTO", 4)) {
    xputs("OpenType font");
  } else if (len>4 && !memcmp(s, "BC\xc0\xde", 4)) {
    xputs("LLVM IR bitcode");
  } else if (strstart(&s, "-----BEGIN CERTIFICATE-----")) {
    xputs("PEM certificate");

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms680547(v=vs.85).aspx
  } else if (len>0x70 && !memcmp(s, "MZ", 2) &&
      (magic=peek_le(s+0x3c,4))<len-4 && !memcmp(s+magic, "\x50\x45\0\0", 4)) {
    xprintf("MS PE32%s executable %s", (peek_le(s+magic+24, 2)==0x20b)?"+":"",
        (peek_le(s+magic+22, 2)&0x2000)?"(DLL) ":"");
    if (peek_le(s+magic+20, 2)>70) {
      char *types[] = {0, "native", "GUI", "console", "OS/2", "driver", "CE",
          "EFI", "EFI boot", "EFI runtime", "EFI ROM", "XBOX", 0, "boot"};
      int type = peek_le(s+magic+92, 2);
      char *name = (type>0 && type<ARRAY_LEN(types))?types[type]:0;

      xprintf("(%s) ", name?name:"unknown");
    }
    xprintf("%s\n", (peek_le(s+magic+4, 2)==0x14c)?"x86":"x86-64");

    // https://en.wikipedia.org/wiki/BMP_file_format
  } else if (len > 0x32 && !memcmp(s, "BM", 2) && !memcmp(s+6, "\0\0\0\0", 4)) {
    int w = peek_le(s+0x12,4), h = peek_le(s+0x16,4), bpp = peek_le(s+0x1c,2);

    xprintf("BMP image, %d x %d, %d bpp\n", w, h, bpp);

    // https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf.data-file-format.txt
  } else if (len>=104 && !memcmp(s, "PERFILE2", 8)) {
    xputs("Linux perf data");

    // https://android.googlesource.com/platform/system/core/+/master/libsparse/sparse_format.h
  } else if (len>28 && peek_le(s, 4) == 0xed26ff3a) {
    xprintf("Android sparse image v%d.%d, %d %d-byte blocks (%d chunks)\n",
        (int) peek_le(s+4, 2), (int) peek_le(s+6, 2), (int) peek_le(s+16, 4),
        (int) peek_le(s+12, 4), (int) peek_le(s+20, 4));

    // https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/master/include/bootimg/bootimg.h
  } else if (len>1632 && !memcmp(s, "ANDROID!", 8)) {
    xprintf("Android boot image v%d\n", (int) peek_le(s+40, 4));

    // https://source.android.com/devices/architecture/dto/partitions
  } else if (len>32 && peek_be(s, 4) == 0xd7b7ab1e) {
    xprintf("Android DTB/DTBO v%d, %d entries\n", (int) peek_be(s+28, 4),
        (int) peek_be(s+16, 4));

  } else {
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
        if ((bytes = utf8towc(&wc, s+i, len-i))>0 && wcwidth(wc)>=0) {
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
    char *name = *arg, *what = "unknown";
    struct stat sb;
    int fd = !strcmp(name, "-");

    if (!FLAG(b))
      xprintf("%s: %*s", name, (int)(TT.max_name_len - strlen(name)), "");

    sb.st_size = 0;
    if (fd || !(FLAG(L) ? stat : lstat)(name, &sb)) {
      if (!fd && !FLAG(s) && (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode))) {
        sprintf(what = toybuf, "%s special (%u/%u)",
            S_ISBLK(sb.st_mode) ? "block" : "character",
            dev_major(sb.st_rdev), dev_minor(sb.st_rdev));
      } else if (fd || S_ISREG(sb.st_mode)) {
        TT.len = sb.st_size;
        // This test identifies an empty file we don't have permission to read
        if (!fd && !sb.st_size) what = "empty";
        else if ((fd = openro(name, O_RDONLY)) != -1) {
          do_regular_file(fd, name);
          if (fd) close(fd);
          continue;
        }
      } else if (S_ISFIFO(sb.st_mode)) what = "fifo";
      else if (S_ISDIR(sb.st_mode)) what = "directory";
      else if (S_ISSOCK(sb.st_mode)) what = "socket";
      else if (S_ISLNK(sb.st_mode)) {
        char *lnk = xreadlink(name);

        sprintf(what = toybuf, "%ssymbolic link to %s",
            stat(name, &sb) ? "broken " : "", lnk);
        free(lnk);
      }
      xputs(what);
    } else xprintf("cannot open: %s\n", strerror(errno));
  }
}
