#!/bin/sh
# Bootstrap toysh and sed without bash, make, sed etc on host.
# Required posix utilities: c99 mkdir printf sh test
# 0BSD 2020 Denys Nykula <nykula@ukr.net>
set -e
CC="${CC:-c99 -D_DEFAULT_SOURCE -Wno-pedantic}"
HOSTCC="${HOSTCC:-c99 -D_DEFAULT_SOURCE -Wno-pedantic}"
export LANG=C LC_ALL=C depends="TOYBOX_HELP TOYBOX_HELP_DASHDASH" toyfiles=
mkdir generated generated/sort
for i in toys/*/*.c; do while read j; do case $j in
  *NEWTOY*MAYFORK*|USE_MKDIR\(*|USE_RM\(*|USE_SED\(*|USE_SH\(*)
    k="${j#USE_}"; depends="$depends ${k%%\(*}"; toyfiles="$toyfiles $i"
    break
esac; done <$i; done

# Make allnoconfig and enable each $depends item.
printf 'config TOYBOX_FORK\n\tbool\n' >generated/Config.probed
printf 'config TOYBOX_ON_ANDROID\n\tbool\n' >>generated/Config.probed
for i in $toyfiles; do k=; while IFS= read -r j; do case $j in
  \*/*) printf \\n; break;;
  'config '*) k=1; printf '# %s\n%s\n' $i "$j";;
  *) if test -n "$k"; then printf %s\\n "$j"; fi
esac; done <$i; done >generated/Config.in
while IFS= read -r i; do case $i in
  *lex.zconf*|*zconf.hash*)
    i="${i#\#include \"}"
    while read -r j; do printf %s\\n "$j"; done <kconfig/${i%\"}_shipped;;
  *) printf %s\\n "$i"
esac; done <kconfig/zconf.tab.c_shipped >kconfig/zconf.tab.c
$HOSTCC -okconfig/conf kconfig/conf.c kconfig/zconf.tab.c \
  -DKBUILD_NO_NLS=1 -DPROJECT_NAME='"ToyBox"'
for j in $depends; do printf CONFIG_%s=y\\n "$j"; done >generated/allno.config
KCONFIG_ALLCONFIG=generated/allno.config kconfig/conf -n Config.in >/dev/null

# Translate kbuild config into a C header.
while read i; do case $i in
  '# CONFIG_'*' is not set')
    i=${i#\# CONFIG_}; i=${i% is not set}
    printf "#define CFG_$i 0\n#define USE_$i(...)\n";;
  CONFIG_*=y)
    i=${i#CONFIG_}; i=${i%=y}
    printf "#define CFG_$i 1\n#define USE_$i(...) __VA_ARGS__\n";;
  CONFIG_*=*)
    i=${i#CONFIG_}
    printf "#define CFG_${i%%=*} ${i#*=}\n";;
esac; done <.config >generated/config.h

# Create a list of all the commands $toyfiles can provide.
printf 'USE_TOYBOX(NEWTOY(toybox, NULL, TOYFLAG_STAYROOT))\n' \
  >generated/newtoys.h
for i in $toyfiles; do while read j; do case $j in USE_*)
  a="${j#*TOY\(}"; a="${j%"$a"}"; c="${j#*,}"; b="${j%%",$c"}"; b="${b#"$a"}"
  printf %s%s,%s\\n "$a" $b "$c" >generated/sort/$b
esac; done <$i; done
for i in generated/sort/*; do read i <$i; printf %s\\n "$i"; done \
  >>generated/newtoys.h

# Process config.h and newtoys.h to generate FLAG_x macros.
(
  printf '#define NEWTOY(aa,bb,cc) aa bb\n#define OLDTOY(...)\n'
  while read i; do case $i in
    *USE_*\)) printf '%s __VA_ARGS__\n' "$i";;
    *) printf %s\\n "$i"
  esac; done <generated/config.h
  printf '#include "lib/toyflags.h"\n#include "generated/newtoys.h"\n'
) |$CROSS_COMPILE$CC -E - |while read i; do while :; do case $i in
  \#*|'') break;;
  *0|*NULL) printf '%s " " " "\n' "${i% *}"; break;;
  *'" "'*) i="${i%%'" "'*}${i#*'" "'}";;
  *'""'*) i="${i%%'""'*}${i#*'""'}";;
  *\") printf '%s %s\n' "$i" "${i#* }"; break;;
  *) break
esac; done; done >generated/flags.raw
$HOSTCC scripts/mkflags.c -ogenerated/mkflags
generated/mkflags <generated/flags.raw >generated/flags.h

# Extract global definitions from $toyfiles and the generated config.
u=; for i in $toyfiles; do n=; while IFS= read -r j; do case $j in
  GLOBALS\()
    n=${i%.*}; n=${n##*/}
    printf '// %s\nstruct %s_data {\n' $i $n
    u="`printf '%s\n  struct %s_data %s;' "$u" $n $n`";;
  \)) if test -n "$n"; then printf '};\n\n'; break; fi;;
  *) if test -n "$n"; then printf %s\\n "$j"; fi
esac; done <$i; done >generated/globals.h
printf 'extern union global_union {%s\n} this;\n' "$u" >>generated/globals.h
>generated/tags.h
$HOSTCC scripts/config2help.c -ogenerated/config2help
generated/config2help Config.in /dev/null >generated/help.h

# Build an intermediate toybox, using which you can build a proper toybox.
$CROSS_COMPILE$CC $CFLAGS -funsigned-char -I. lib/*.c main.c \
  $toyfiles $LDFLAGS -otoybox; ./toybox rm -r .config generated
