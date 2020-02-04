/* readelf.c - display information about ELF files.
 *
 * Copyright 2019 The Android Open Source Project
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/nm.html

USE_READELF(NEWTOY(readelf, "<1(dyn-syms)adhlnp:SsWx:", TOYFLAG_USR|TOYFLAG_BIN))

config READELF
  bool "readelf"
  default y
  help
    usage: readelf [-adhlnSsW] [-p SECTION] [-x SECTION] [file...]

    Displays information about ELF files.

    -a	Equivalent to -dhlnSs
    -d	Show dynamic section
    -h	Show ELF header
    -l	Show program headers
    -n	Show notes
    -p S	Dump strings found in named/numbered section
    -S	Show section headers
    -s	Show symbol tables (.dynsym and .symtab)
    -W	Don't truncate fields (default in toybox)
    -x S	Hex dump of named/numbered section

    --dyn-syms	Show just .dynsym symbol table
*/

#define FOR_readelf
#include "toys.h"

GLOBALS(
  char *x, *p;

  char *elf, *shstrtab, *f;
  long long shoff, phoff, size;
  int bits, shnum, shentsize, phentsize;
  int64_t (*elf_int)(void *ptr, unsigned size);
)

// Section header.
struct sh {
  int type, link, info;
  long long flags, addr, offset, size, addralign, entsize;
  char *name;
};

// Program header.
struct ph {
  int type, flags;
  long long offset, vaddr, paddr, filesz, memsz, align;
};

static void get_sh(int i, struct sh *s)
{
  char *shdr = TT.elf+TT.shoff+i*TT.shentsize;

  if (i >= TT.shnum || shdr > TT.elf+TT.size-TT.shentsize) {
    error_exit("%s: bad shdr %d",TT.f,i);
  }

  s->type = TT.elf_int(shdr+4, 4);
  s->flags = TT.elf_int(shdr+8, 4*(TT.bits+1));
  s->addr = TT.elf_int(shdr+8+4*(TT.bits+1), 4*(TT.bits+1));
  s->offset = TT.elf_int(shdr+8+8*(TT.bits+1), 4*(TT.bits+1));
  s->size = TT.elf_int(shdr+8+12*(TT.bits+1), 4*(TT.bits+1));
  s->link = TT.elf_int(shdr+8+16*(TT.bits+1), 4);
  s->info = TT.elf_int(shdr+12+16*(TT.bits+1), 4);
  s->addralign = TT.elf_int(shdr+16+16*(TT.bits+1), 4*(TT.bits+1));
  s->entsize = TT.elf_int(shdr+16+20*(TT.bits+1), 4*(TT.bits+1));

  if (!TT.shstrtab) s->name = "?";
  else {
    s->name = TT.shstrtab + TT.elf_int(shdr, 4);
    if (s->name >= TT.elf+TT.size) error_exit("%s: bad shdr name %d",TT.f,i);
    if (s->offset >= TT.size-s->size && s->type != 8 /*SHT_NOBITS*/)
      error_exit("%s: bad section %d",TT.f,i);
  }
}

static int find_section(char *spec, struct sh *s)
{
  char *end;
  int i;

  // Valid section number?
  errno = 0;
  i = strtoul(spec, &end, 0);
  if (!errno && !*end && i < TT.shnum) {
    get_sh(i, s);
    return 1;
  }

  // Search the section names.
  for (i=0; i<TT.shnum; i++) {
    get_sh(i, s);
    if (!strcmp(s->name, spec)) return 1;
  }

  error_msg("%s: no section '%s", TT.f, spec);
  return 0;
}

static void get_ph(int i, struct ph *ph)
{
  char *phdr = TT.elf+TT.phoff+i*TT.phentsize;

  if (phdr > TT.elf+TT.size-TT.phentsize) error_exit("%s: bad phdr %d",TT.f,i);

  // Elf64_Phdr reordered fields.
  ph->type = TT.elf_int(phdr, 4);
  if (TT.bits) {
    ph->flags = TT.elf_int(phdr+=4, 4);
    ph->offset = TT.elf_int(phdr+=4, 8);
    ph->vaddr = TT.elf_int(phdr+=8, 8);
    ph->paddr = TT.elf_int(phdr+=8, 8);
    ph->filesz = TT.elf_int(phdr+=8, 8);
    ph->memsz = TT.elf_int(phdr+=8, 8);
    ph->align = TT.elf_int(phdr+=8, 8);
  } else {
    ph->offset = TT.elf_int(phdr+=4, 4);
    ph->vaddr = TT.elf_int(phdr+=4, 4);
    ph->paddr = TT.elf_int(phdr+=4, 4);
    ph->filesz = TT.elf_int(phdr+=4, 4);
    ph->memsz = TT.elf_int(phdr+=4, 4);
    ph->flags = TT.elf_int(phdr+=4, 4);
    ph->align = TT.elf_int(phdr+=4, 4);
  }
}

#define MAP(...) __VA_ARGS__
#define DECODER(name, values) \
  static char *name(int type) { \
    static char unknown[20]; \
    struct {int v; char *s;} a[] = values; \
    int i; \
    \
    for (i=0; i<ARRAY_LEN(a); i++) if (type==a[i].v) return a[i].s; \
    sprintf(unknown, "0x%x", type); \
    return unknown; \
  }

DECODER(dt_type, MAP({{0,"x(NULL)"},{1,"N(NEEDED)"},{2,"b(PLTRELSZ)"},
  {3,"x(PLTGOT)"},{4,"x(HASH)"},{5,"x(STRTAB)"},{6,"x(SYMTAB)"},{7,"x(RELA)"},
  {8,"b(RELASZ)"},{9,"b(RELAENT)"},{10,"b(STRSZ)"},{11,"b(SYMENT)"},
  {12,"x(INIT)"},{13,"x(FINI)"},{14,"S(SONAME)"},{15,"R(RPATH)"},
  {16,"x(SYMBOLIC)"},{17,"x(REL)"},{18,"b(RELSZ)"},{19,"b(RELENT)"},
  {20,"P(PLTREL)"},{21,"x(DEBUG)"},{22,"x(TEXTREL)"},{23,"x(JMPREL)"},
  {24,"d(BIND_NOW)"},{25,"x(INIT_ARRAY)"},{26,"x(FINI_ARRAY)"},
  {27,"b(INIT_ARRAYSZ)"},{28,"b(FINI_ARRAYSZ)"},{29,"R(RUNPATH)"},
  {30,"f(FLAGS)"},{32,"x(PREINIT_ARRAY)"},{33,"x(PREINIT_ARRAYSZ)"},
  {35,"b(RELRSZ)"},{36,"x(RELR)"},{37,"b(RELRENT)"},
  {0x6000000f,"x(ANDROID_REL)"},{0x60000010,"b(ANDROID_RELSZ)"},
  {0x60000011,"x(ANDROID_RELA)"},{0x60000012,"b(ANDROID_RELASZ)"},
  {0x6fffe000,"x(ANDROID_RELR)"},{0x6fffe001,"b(ANDROID_RELRSZ)"},
  {0x6fffe003,"x(ANDROID_RELRENT)"},{0x6ffffef5,"x(GNU_HASH)"},
  {0x6ffffef6,"x(TLSDESC_PLT)"},{0x6ffffef7,"x(TLSDESC_GOT)"},
  {0x6ffffff0,"x(VERSYM)"},{0x6ffffff9,"d(RELACOUNT)"},
  {0x6ffffffa,"d(RELCOUNT)"},{0x6ffffffb,"F(FLAGS_1)"},
  {0x6ffffffc," (VERDEF)"},{0x6ffffffd,"d(VERDEFNUM)"},
  {0x6ffffffe,"x(VERNEED)"},{0x6fffffff,"d(VERNEEDNUM)"}}))

DECODER(et_type, MAP({{0,"NONE (None)"},{1,"REL (Relocatable file)"},
  {2,"EXEC (Executable file)"},{3,"DYN (Shared object file)"},
  {4,"CORE (Core file)"}}))

DECODER(nt_type_core, MAP({{1,"NT_PRSTATUS"},{2,"NT_FPREGSET"},
  {3,"NT_PRPSINFO"},{5,"NT_PLATFORM"},{6,"NT_AUXV"},
  {0x46494c45,"NT_FILE"},{0x53494749,"NT_SIGINFO"}}))

DECODER(nt_type_linux, MAP({{0x200,"NT_386_TLS"},{0x202, "NT_X86_XSTATE"},
  {0x400,"NT_ARM_VFP"},{0x401,"NT_ARM_TLS"},{0x405,"NT_ARM_SVE"}}))

DECODER(os_abi, MAP({{0,"UNIX - System V"}}))

DECODER(ph_type, MAP({{0,"NULL"},{1,"LOAD"},{2,"DYNAMIC"},{3,"INTERP"},
  {4,"NOTE"},{5,"SHLIB"},{6,"PHDR"},{7,"TLS"},{0x6474e550,"GNU_EH_FRAME"},
  {0x6474e551,"GNU_STACK"},{0x6474e552,"GNU_RELRO"},{0x70000001,"EXIDX"}}))

DECODER(sh_type, MAP({{0,"NULL"},{1,"PROGBITS"},{2,"SYMTAB"},{3,"STRTAB"},
  {4,"RELA"},{5,"HASH"},{6,"DYNAMIC"},{7,"NOTE"},{8,"NOBITS"},{9,"REL"},
  {10,"SHLIB"},{11,"DYNSYM"},{14,"INIT_ARRAY"},{15,"FINI_ARRAY"},
  {16,"PREINIT_ARRAY"},{17,"GROUP"},{18,"SYMTAB_SHNDX"},{19,"RELR"},
  {0x60000001,"ANDROID_REL"},{0x60000002,"ANDROID_RELA"},
  {0x6fffff00,"ANDROID_RELR"},{0x6ffffff6,"GNU_HASH"},
  {0x6ffffffd,"VERDEF"},{0x6ffffffe,"VERNEED"},
  {0x6fffffff,"VERSYM"},{0x70000001,"ARM_EXIDX"},
  {0x70000003,"ARM_ATTRIBUTES"}}))

DECODER(stb_type, MAP({{0,"LOCAL"},{1,"GLOBAL"},{2,"WEAK"}}))

DECODER(stt_type, MAP({{0,"NOTYPE"},{1,"OBJECT"},{2,"FUNC"},{3,"SECTION"},
  {4,"FILE"},{5,"COMMON"},{6,"TLS"},{10,"GNU_IFUNC"}}))

DECODER(stv_type, MAP({{0,"DEFAULT"},{1,"INTERNAL"},{2,"HIDDEN"},
  {3,"PROTECTED"}}))

static void show_symbols(struct sh *table, struct sh *strtab)
{
  char *symtab = TT.elf+table->offset, *ndx;
  int sym_size = (TT.bits ? 24 : 16), numsym = table->size/sym_size, i;

  if (numsym == 0) return;

  xputc('\n');
  printf("Symbol table '%s' contains %d entries:\n"
         "   Num:    %*s  Size Type    Bind   Vis      Ndx Name\n",
         table->name, numsym, 5+8*TT.bits, "Value");
  for (i=0; i<numsym; i++) {
    int st_name = TT.elf_int(symtab, 4), st_value, st_shndx;
    unsigned char st_info, st_other;
    long st_size;
    char *name;

    // The various fields were moved around for 64-bit.
    if (TT.bits) {
      st_info = symtab[4];
      st_other = symtab[5];
      st_shndx = TT.elf_int(symtab+6, 2);
      st_value = TT.elf_int(symtab+8, 8);
      st_size = TT.elf_int(symtab+16, 8);
    } else {
      st_value = TT.elf_int(symtab+4, 4);
      st_size = TT.elf_int(symtab+8, 4);
      st_info = symtab[12];
      st_other = symtab[13];
      st_shndx = TT.elf_int(symtab+14, 2);
    }

    name = TT.elf + strtab->offset + st_name;
    if (name >= TT.elf+TT.size) error_exit("%s: bad symbol name", TT.f);

    if (!st_shndx) ndx = "UND";
    else if (st_shndx==0xfff1) ndx = "ABS";
    else sprintf(ndx = toybuf, "%d", st_shndx);

    // TODO: look up and show any symbol versions with @ or @@.

    printf("%6d: %0*x %5ld %-7s %-6s %-9s%3s %s\n", i, 8*(TT.bits+1),
      st_value, st_size, stt_type(st_info & 0xf), stb_type(st_info >> 4),
      stv_type(st_other & 3), ndx, name);
    symtab += sym_size;
  }
}

static void show_notes(long offset, long size)
{
  char *note = TT.elf + offset;

  printf("  %-20s %10s\tDescription\n", "Owner", "Data size");
  while (note < TT.elf+offset+size) {
    int namesz = TT.elf_int(note, 4), descsz = TT.elf_int(note+4, 4),
        type = TT.elf_int(note+8, 4), j = 0;
    char *name = note+12;

    printf("  %-20.*s 0x%08x\t", namesz, name, descsz);
    if (!memcmp(name, "GNU", 4)) {
      if (type == 1) {
        printf("NT_GNU_ABI_TAG\tOS: %s, ABI: %d.%d.%d",
               !TT.elf_int(note+16, 4)?"Linux":"?",
               (int)TT.elf_int(note+20, 4), (int)TT.elf_int(note+24, 4),
               (int)TT.elf_int(note+28, 4)), j=1;
      } else if (type == 3) {
        printf("NT_GNU_BUILD_ID\t");
        for (;j<descsz;j++) printf("%02x",note[16+j]);
      } else if (type == 4) {
        printf("NT_GNU_GOLD_VERSION\t%.*s", descsz, note+16), j=1;
      }
    } else if (!memcmp(name, "Android", 8)) {
      if (type == 1) {
        printf("NT_VERSION\tAPI level %d", (int)TT.elf_int(note+20, 4)), j=1;
        if (descsz>=132) printf(", NDK %.64s (%.64s)",note+24,note+24+64);
      }
    } else if (!memcmp(name, "CORE", 5) || !memcmp(name, "LINUX", 6)) {
      char *desc = *name=='C' ? nt_type_core(type) : nt_type_linux(type);

      if (*desc != '0') printf("%s", desc), j=1;
    }

    // If we didn't do custom output above, show a hex dump.
    if (!j) {
      printf("0x%x\t", type);
      for (;j<descsz;j++) printf("%c%02x",!j?'\t':' ',note[16+j]);
    }
    xputc('\n');

    note += 3*4 + ((namesz+3)&~3) + ((descsz+3)&~3);
  }
}

static void scan_elf()
{
  struct sh dynamic = {}, dynstr = {}, dynsym = {}, shstr = {}, strtab = {},
    symtab = {}, s;
  struct ph ph;
  int endian, version, elf_type, flags, entry, ehsize, phnum, shstrndx, i,j,w;

  if (TT.size < 45 || memcmp(TT.elf, "\177ELF", 4)) {
    error_msg("%s: not ELF", TT.f);
    return;
  }

  TT.bits = TT.elf[4] - 1;
  endian = TT.elf[5];
  version = TT.elf[6];
  TT.elf_int = (endian==2) ? peek_be : peek_le;
  if (TT.bits < 0 || TT.bits > 1 || endian < 1 || endian > 2 || version != 1) {
    error_msg("%s: bad ELF", TT.f);
    return;
  }

  elf_type = TT.elf_int(TT.elf+16, 2);
  entry = TT.elf_int(TT.elf+24, 4+4*TT.bits);
  TT.phoff = TT.elf_int(TT.elf+28+4*TT.bits, 4+4*TT.bits);
  TT.shoff = TT.elf_int(TT.elf+32+8*TT.bits, 4+4*TT.bits);
  flags = TT.elf_int(TT.elf+36+12*TT.bits, 4);
  ehsize = TT.elf_int(TT.elf+40+12*TT.bits, 2);
  TT.phentsize = TT.elf_int(TT.elf+42+12*TT.bits, 2);
  phnum = TT.elf_int(TT.elf+44+12*TT.bits, 2);
  TT.shentsize = TT.elf_int(TT.elf+46+12*TT.bits, 2);
  TT.shnum = TT.elf_int(TT.elf+48+12*TT.bits, 2);
  shstrndx = TT.elf_int(TT.elf+50+12*TT.bits, 2);

  // Set up the section header string table so we can use section header names.
  // Core files have shstrndx == 0.
  TT.shstrtab = 0;
  if (shstrndx != 0) {
    get_sh(shstrndx, &shstr);
    if (shstr.type != 3 /*SHT_STRTAB*/) {
      error_msg("%s: bad shstrndx", TT.f);
      return;
    }
    TT.shstrtab = TT.elf+shstr.offset;
  }

  if (toys.optc > 1) printf("\nFile: %s\n", TT.f);

  if (FLAG(h)) {
    printf("ELF Header:\n");
    printf("  Magic:   ");
    for (i=0; i<16; i++) printf("%02x%c", TT.elf[i], i==15?'\n':' ');
    printf("  Class:                             ELF%d\n", TT.bits?64:32);
    printf("  Data:                              2's complement, %s endian\n",
           (endian==2)?"big":"little");
    printf("  Version:                           1 (current)\n");
    printf("  OS/ABI:                            %s\n", os_abi(TT.elf[7]));
    printf("  ABI Version:                       %d\n", TT.elf[8]);
    printf("  Type:                              %s\n", et_type(elf_type));
    printf("  Machine:                           %s\n",
           elf_arch_name(TT.elf_int(TT.elf+18, 2)));
    printf("  Version:                           0x%x\n",
           (int) TT.elf_int(TT.elf+20, 4));
    printf("  Entry point address:               0x%x\n", entry);
    printf("  Start of program headers:          %lld (bytes into file)\n",
           TT.phoff);
    printf("  Start of section headers:          %lld (bytes into file)\n",
           TT.shoff);
    printf("  Flags:                             0x%x\n", flags);
    printf("  Size of this header:               %d (bytes)\n", ehsize);
    printf("  Size of program headers:           %d (bytes)\n", TT.phentsize);
    printf("  Number of program headers:         %d\n", phnum);
    printf("  Size of section headers:           %d (bytes)\n", TT.shentsize);
    printf("  Number of section headers:         %d\n", TT.shnum);
    printf("  Section header string table index: %d\n", shstrndx);
  }

  w = 8*(TT.bits+1);
  if (FLAG(S)) {
    if (!TT.shnum) printf("\nThere are no sections in this file.\n");
    else {
      if (!FLAG(h)) {
        printf("There are %d section headers, starting at offset %#llx:\n",
               TT.shnum, TT.shoff);
      }
      printf("\n"
             "Section Headers:\n"
             "  [Nr] %-20s %-14s %-*s %-6s %-6s ES Flg Lk Inf Al\n",
             "Name", "Type", w, "Address", "Off", "Size");
    }
  }
  // We need to iterate through the section headers even if we're not
  // dumping them, to find specific sections.
  for (i=0; i<TT.shnum; i++) {
    get_sh(i, &s);
    if (s.type == 2 /*SHT_SYMTAB*/) symtab = s;
    else if (s.type == 6 /*SHT_DYNAMIC*/) dynamic = s;
    else if (s.type == 11 /*SHT_DYNSYM*/) dynsym = s;
    else if (s.type == 3 /*SHT_STRTAB*/) {
      if (!strcmp(s.name, ".strtab")) strtab = s;
      else if (!strcmp(s.name, ".dynstr")) dynstr = s;
    }

    if (FLAG(S)) {
      char sh_flags[12] = {}, *p = sh_flags;

      for (j=0; j<12; j++) if (s.flags&(1<<j)) *p++="WAXxMSILOTC"[j];
      printf("  [%2d] %-20s %-14s %0*llx %06llx %06llx %02llx %3s %2d %2d %2lld\n",
             i, s.name, sh_type(s.type), w, s.addr, s.offset, s.size,
             s.entsize, sh_flags, s.link, s.info, s.addralign);
    }
  }
  if (FLAG(S) && TT.shnum) {
    printf("Key:\n"
           "  (W)rite, (A)lloc, e(X)ecute, (M)erge, (S)trings, (I)nfo\n"
           "  (L)ink order, (O)S, (G)roup, (T)LS, (C)ompressed, x=unknown\n");
  }

  if (FLAG(l)) {
    xputc('\n');
    if (!phnum) printf("There are no program headers in this file.\n");
    else {
      if (!FLAG(h)) {
        printf("Elf file type is %s\n"
        "Entry point %#x\n"
        "There are %d program headers, starting at offset %lld\n"
        "\n",
        et_type(elf_type), entry, phnum, TT.phoff);
      }
      printf("Program Headers:\n"
             "  %-14s %-8s %-*s   %-*s   %-7s %-7s Flg Align\n", "Type",
             "Offset", w, "VirtAddr", w, "PhysAddr", "FileSiz", "MemSiz");
      for (i=0; i<phnum; i++) {
        get_ph(i, &ph);
        printf("  %-14s 0x%06llx 0x%0*llx 0x%0*llx 0x%05llx 0x%05llx %c%c%c %#llx\n",
               ph_type(ph.type), ph.offset, w, ph.vaddr, w, ph.paddr,
               ph.filesz, ph.memsz, ph.flags&4?'R':' ', ph.flags&2?'W':' ',
               ph.flags&1?'E':' ', ph.align);
        if (ph.type == 3 /*PH_INTERP*/) {
          printf("      [Requesting program interpreter: %*s]\n",
                 (int) ph.filesz-1, TT.elf+ph.offset);
        }
      }

      printf("\n"
             " Section to Segment mapping:\n"
             "  Segment Sections...\n");
      for (i=0; i<phnum; i++) {
        get_ph(i, &ph);
        printf("   %02d    ", i);
        for (j=0; j<TT.shnum; j++) {
          get_sh(j, &s);
          if (s.offset >= ph.offset && s.offset+s.size <= ph.offset+ph.filesz)
            printf(" %s", s.name);
        }
        xputc('\n');
      }
    }
  }

  // binutils ld emits a bunch of extra DT_NULL entries, so binutils readelf
  // uses two passes here! We just tell the truth, which matches -h.
  if (FLAG(d)) {
    char *dyn = TT.elf+dynamic.offset, *end = dyn+dynamic.size;

    xputc('\n');
    if (!dynamic.size) printf("There is no dynamic section in this file.\n");
    else printf("Dynamic section at offset 0x%llx contains %lld entries:\n"
                "  %-*s %-20s %s\n",
                dynamic.offset, dynamic.size/dynamic.entsize,
                w+2, "Tag", "Type", "Name/Value");
    for (; dyn < end; dyn += dynamic.entsize) {
      int es = 4*(TT.bits+1);
      long tag = TT.elf_int(dyn, es), val = TT.elf_int(dyn+es, es);
      char *type = dt_type(tag);

      printf(" 0x%0*lx %-20s ", w, tag, *type=='0' ? type : type+1);
      if (*type == 'd') printf("%ld\n", val);
      else if (*type == 'b') printf("%ld (bytes)\n", val);
      else if (*type == 's') printf("%s\n", TT.elf+dynstr.offset+val);
      else if (*type == 'f' || *type == 'F') {
        struct bitname { int bit; char *s; }
          df_names[] = {{0, "ORIGIN"},{1,"SYMBOLIC"},{2,"TEXTREL"},
            {3,"BIND_NOW"},{4,"STATIC_TLS"},{}},
          df_1_names[]={{0,"NOW"},{1,"GLOBAL"},{2,"GROUP"},{3,"NODELETE"},
            {5,"INITFIRST"},{27,"PIE"},{}},
          *names = *type == 'f' ? df_names : df_1_names;
        int mask;

        if (*type == 'F') printf("Flags: ");
        for (j=0; names[j].s; j++) {
          if (val & (mask=(1<<names[j].bit))) {
            printf("%s%s", names[j].s, (val &= ~mask) ? " " : "");
          }
        }
        if (val) printf("0x%lx", val);
        xputc('\n');
      } else if (*type == 'N' || *type == 'R' || *type == 'S') {
        printf("%s: [%s]\n", *type=='N' ? "Shared library" :
          (*type=='R' ? "Library runpath" : "Library soname"),
          TT.elf+dynstr.offset+val);
      } else if (*type == 'P') {
        type = dt_type(val);
        j = strlen(type);
        if (*type != '0') type += 2, j -= 3;
        printf("%*.*s\n", j, j, type);
      } else printf("0x%lx\n", val);
    }
  }

  if (FLAG(dyn_syms)) show_symbols(&dynsym, &dynstr);
  if (FLAG(s)) show_symbols(&symtab, &strtab);

  if (FLAG(n)) {
    int found = 0;

    for (i=0; i<TT.shnum; i++) {
      get_sh(i, &s);
      if (s.type == 7 /*SHT_NOTE*/) {
        printf("\nDisplaying notes found in: %s\n", s.name);
        show_notes(s.offset, s.size);
        found = 1;
      }
    }
    for (i=0; !found && i<phnum; i++) {
      get_ph(i, &ph);
      if (ph.type == 4 /*PT_NOTE*/) {
        printf("\n"
          "Displaying notes found at file offset 0x%llx with length 0x%llx:\n",
          ph.offset, ph.filesz);
        show_notes(ph.offset, ph.filesz);
      }
    }
  }

  if (FLAG(x)) {
    if (find_section(TT.x, &s)) {
      char *p = TT.elf+s.offset;
      long offset = 0;

      printf("\nHex dump of section '%s':\n", s.name);
      while (offset < s.size) {
        int space = 2*16 + 16/4;

        printf("  0x%08lx ", offset);
        for (i=0; i<16 && offset < s.size; offset++) {
          space -= printf("%02x%s", *p++, ++i%4 ? "" : " ");
        }
        printf("%*s", space, "");
        for (p-=i; i; i--, p++) putchar(*p>=' ' && *p<='~' ? *p : '.');
        xputc('\n');
      }
      printf("\n");
    }
  }

  if (FLAG(p)) {
    if (find_section(TT.p, &s)) {
      char *begin = TT.elf+s.offset, *end = begin + s.size, *p = begin;
      int any = 0;

      printf("\nString dump of section '%s':\n", s.name);
      for (; p < end; p++) {
        if (isprint(*p)) {
          printf("  [%6tx]  ", p-begin);
          while (p < end && isprint(*p)) putchar(*p++);
          xputc('\n');
          any=1;
        }
      }
      if (!any) printf("  No strings found in this section.\n");
      printf("\n");
    }
  }
}

void readelf_main(void)
{
  char **arg;
  int all = FLAG_d|FLAG_h|FLAG_l|FLAG_n|FLAG_S|FLAG_s|FLAG_dyn_syms;

  if (FLAG(a)) toys.optflags |= all;
  if (FLAG(s)) toys.optflags |= FLAG_dyn_syms;
  if (!(toys.optflags & (all|FLAG_p|FLAG_x))) help_exit("needs a flag");

  for (arg = toys.optargs; *arg; arg++) {
    int fd = open(TT.f = *arg, O_RDONLY);
    struct stat sb;

    if (fd == -1) perror_msg("%s", TT.f);
    else {
      if (fstat(fd, &sb)) perror_msg("%s", TT.f);
      else if (!sb.st_size) error_msg("%s: empty", TT.f);
      else if (!S_ISREG(sb.st_mode)) error_msg("%s: not a regular file",TT.f);
      else {
        TT.elf = xmmap(NULL, TT.size=sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
        scan_elf();
        munmap(TT.elf, TT.size);
      }
      close(fd);
    }
  }
}
