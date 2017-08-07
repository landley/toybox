#!/bin/bash

# Grab default values for $CFLAGS and such.

export LANG=c
export LC_ALL=C
set -o pipefail
source ./configure

[ -z "$KCONFIG_CONFIG" ] && KCONFIG_CONFIG=.config
[ -z "$OUTNAME" ] && OUTNAME=toybox
UNSTRIPPED="generated/unstripped/$(basename "$OUTNAME")"

# Since each cc invocation is short, launch half again as many processes
# as we have processors so they don't exit faster than we can start them.
[ -z "$CPUS" ] && CPUS=$(($(nproc)+1))

if [ -z "$SED" ]
then
  [ ! -z "$(which gsed 2>/dev/null)" ] && SED=gsed || SED=sed
fi

# Respond to V= by echoing command lines as well as running them
DOTPROG=
do_loudly()
{
  [ ! -z "$V" ] && echo "$@" || echo -n "$DOTPROG"
  "$@"
}

# Is anything under directory $2 newer than file $1
isnewer()
{
  CHECK="$1"
  shift
  [ ! -z "$(find "$@" -newer "$CHECK" 2>/dev/null || echo yes)" ]
}

echo "Generate headers from toys/*/*.c..."

mkdir -p generated/unstripped

if isnewer generated/Config.in toys
then
  echo "Extract configuration information from toys/*.c files..."
  scripts/genconfig.sh
fi

# Create a list of all the commands toybox can provide. Note that the first
# entry is out of order on purpose (the toybox multiplexer command must be the
# first element of the array). The rest must be sorted in alphabetical order
# for fast binary search.

if isnewer generated/newtoys.h toys
then
  echo -n "generated/newtoys.h "

  echo "USE_TOYBOX(NEWTOY(toybox, NULL, TOYFLAG_STAYROOT))" > generated/newtoys.h
  $SED -n -e 's/^USE_[A-Z0-9_]*(/&/p' toys/*/*.c \
	| $SED 's/\(.*TOY(\)\([^,]*\),\(.*\)/\2 \1\2,\3/' | sort -s -k 1,1 \
	| $SED 's/[^ ]* //'  >> generated/newtoys.h
  [ $? -ne 0 ] && exit 1
fi

[ ! -z "$V" ] && echo "Which C files to build..."

# Extract a list of toys/*/*.c files to compile from the data in $KCONFIG_CONFIG
# (First command names, then filenames with relevant {NEW,OLD}TOY() macro.)

[ -d ".git" ] && GITHASH="$(git describe --tags --abbrev=12 2>/dev/null)"
[ ! -z "$GITHASH" ] && GITHASH="-DTOYBOX_VERSION=\"$GITHASH\""
TOYFILES="$($SED -n 's/^CONFIG_\([^=]*\)=.*/\1/p' "$KCONFIG_CONFIG" | xargs | tr ' [A-Z]' '|[a-z]')"
TOYFILES="$(egrep -l "TOY[(]($TOYFILES)[ ,]" toys/*/*.c)"
CFLAGS="$CFLAGS $(cat generated/cflags)"
BUILD="$(echo ${CROSS_COMPILE}${CC} $CFLAGS -I . $OPTIMIZE $GITHASH)"
LIBFILES="$(ls lib/*.c | grep -v lib/help.c)"
TOYFILES="lib/help.c main.c $TOYFILES"

if [ "${TOYFILES/pending//}" != "$TOYFILES" ]
then
  echo -e "\n\033[1;31mwarning: using unfinished code from toys/pending\033[0m"
fi

genbuildsh()
{
  # Write a canned build line for use on crippled build machines.

  echo "#!/bin/sh"
  echo
  echo "BUILD='$BUILD'"
  echo
  echo "FILES='$LIBFILES $TOYFILES'"
  echo
  echo "LINK='$LINK'"
  echo
  echo
  echo '$BUILD $FILES $LINK'
}

if ! cmp -s <(genbuildsh | head -n 3) \
          <(head -n 3 generated/build.sh 2>/dev/null)
then
  echo -n "Library probe"

  # We trust --as-needed to remove each library if we don't use any symbols
  # out of it, this loop is because the compiler has no way to ignore a library
  # that doesn't exist, so we have to detect and skip nonexistent libraries
  # for it.

  > generated/optlibs.dat
  for i in util crypt m resolv selinux smack attr rt crypto z log
  do
    echo "int main(int argc, char *argv[]) {return 0;}" | \
    ${CROSS_COMPILE}${CC} $CFLAGS -xc - -o generated/libprobe -Wl,--as-needed -l$i > /dev/null 2>/dev/null &&
    echo -l$i >> generated/optlibs.dat
    echo -n .
  done
  rm -f generated/libprobe
  echo
fi

# LINK needs optlibs.dat, above

LINK="$(echo $LDOPTIMIZE $LDFLAGS -o "$UNSTRIPPED" -Wl,--as-needed $(cat generated/optlibs.dat))"
genbuildsh > generated/build.sh && chmod +x generated/build.sh || exit 1

#TODO: "make $SED && make" doesn't regenerate config.h because diff .config
if true #isnewer generated/config.h "$KCONFIG_CONFIG"
then
  echo "Make generated/config.h from $KCONFIG_CONFIG."

  # This long and roundabout sed invocation is to make old versions of sed
  # happy. New ones have '\n' so can replace one line with two without all
  # the branches and tedious mucking about with hold space.

  $SED -n \
    -e 's/^# CONFIG_\(.*\) is not set.*/\1/' \
    -e 't notset' \
    -e 's/^CONFIG_\(.*\)=y.*/\1/' \
    -e 't isset' \
    -e 's/^CONFIG_\([^=]*\)=\(.*\)/#define CFG_\1 \2/p' \
    -e 'd' \
    -e ':notset' \
    -e 'h' \
    -e 's/.*/#define CFG_& 0/p' \
    -e 'g' \
    -e 's/.*/#define USE_&(...)/p' \
    -e 'd' \
    -e ':isset' \
    -e 'h' \
    -e 's/.*/#define CFG_& 1/p' \
    -e 'g' \
    -e 's/.*/#define USE_&(...) __VA_ARGS__/p' \
    $KCONFIG_CONFIG > generated/config.h || exit 1
fi

if [ generated/mkflags -ot scripts/mkflags.c ]
then
  do_loudly $HOSTCC scripts/mkflags.c -o generated/mkflags || exit 1
fi

# Process config.h and newtoys.h to generate FLAG_x macros. Note we must
# always #define the relevant macro, even when it's disabled, because we
# allow multiple NEWTOY() in the same C file. (When disabled the FLAG is 0,
# so flags&0 becomes a constant 0 allowing dead code elimination.)

make_flagsh()
{
  # Parse files through C preprocessor twice, once to get flags for current
  # .config and once to get flags for allyesconfig
  for I in A B
  do
    (
    # define macros and select header files with option string data

    echo "#define NEWTOY(aa,bb,cc) aa $I bb"
    echo '#define OLDTOY(...)'
    if [ "$I" == A ]
    then
      cat generated/config.h
    else
      $SED '/USE_.*([^)]*)$/s/$/ __VA_ARGS__/' generated/config.h
    fi
    echo '#include "lib/toyflags.h"'
    cat generated/newtoys.h

    # Run result through preprocessor, glue together " " gaps leftover from USE
    # macros, delete comment lines, print any line with a quoted optstring,
    # turn any non-quoted opstring (NULL or 0) into " " (because fscanf can't
    # handle "" with nothing in it, and mkflags uses that).

    ) | ${CROSS_COMPILE}${CC} -E - | \
    $SED -n -e 's/" *"//g;/^#/d;t clear;:clear;s/"/"/p;t;s/\( [AB] \).*/\1 " "/p'

  # Sort resulting line pairs and glue them together into triplets of
  #   command "flags" "allflags"
  # to feed into mkflags C program that outputs actual flag macros
  # If no pair (because command's disabled in config), use " " for flags
  # so allflags can define the appropriate zero macros.

  done | sort -s | $SED -n -e 's/ A / /;t pair;h;s/\([^ ]*\).*/\1 " "/;x' \
    -e 'b single;:pair;h;n;:single;s/[^ ]* B //;H;g;s/\n/ /;p' | \
    tee generated/flags.raw | generated/mkflags > generated/flags.h || exit 1
}

if isnewer generated/flags.h toys "$KCONFIG_CONFIG"
then
  echo -n "generated/flags.h "
  make_flagsh
fi

# Extract global structure definitions and flag definitions from toys/*/*.c

function getglobals()
{
  for i in toys/*/*.c
  do
    NAME="$(echo $i | $SED 's@.*/\(.*\)\.c@\1@')"
    DATA="$($SED -n -e '/^GLOBALS(/,/^)/b got;b;:got' \
            -e 's/^GLOBALS(/struct '"$NAME"'_data {/' \
            -e 's/^)/};/' -e 'p' $i)"

    [ ! -z "$DATA" ] && echo -e "// $i\n\n$DATA\n"
  done
}

if isnewer generated/globals.h toys
then
  echo -n "generated/globals.h "
  GLOBSTRUCT="$(getglobals)"
  (
    echo "$GLOBSTRUCT"
    echo
    echo "extern union global_union {"
    echo "$GLOBSTRUCT" | \
      $SED -n 's/struct \(.*\)_data {/	struct \1_data \1;/p'
    echo "} this;"
  ) > generated/globals.h
fi

if [ generated/mktags -ot scripts/mktags.c ]
then
  do_loudly $HOSTCC scripts/mktags.c -o generated/mktags || exit 1
fi

if isnewer generated/tags.h toys
then
  echo -n "generated/tags.h "

  $SED -n '/TAGGED_ARRAY(/,/^)/{s/.*TAGGED_ARRAY[(]\([^,]*\),/\1/;p}' \
    toys/*/*.c lib/*.c | generated/mktags > generated/tags.h
fi

if [ generated/config2help -ot scripts/config2help.c ]
then
  do_loudly $HOSTCC scripts/config2help.c -I . lib/xwrap.c lib/llist.c \
    lib/lib.c lib/portability.c -o generated/config2help || exit 1
fi
if isnewer generated/help.h generated/Config.in
then
  echo "generated/help.h"
  generated/config2help Config.in $KCONFIG_CONFIG > generated/help.h || exit 1
fi

[ ! -z "$NOBUILD" ] && exit 0

echo -n "Compile toybox"
[ ! -z "$V" ] && echo
DOTPROG=.

# This is a parallel version of: do_loudly $BUILD $FILES $LINK || exit 1

# Any headers newer than the oldest generated/obj file?
X="$(ls -1t generated/obj/* 2>/dev/null | tail -n 1)"
# TODO: redo this
if [ ! -e "$X" ] || [ ! -z "$(find toys -name "*.h" -newer "$X")" ]
then
  rm -rf generated/obj && mkdir -p generated/obj || exit 1
else
  rm -f generated/obj/{main,lib_help}.o || exit 1
fi

# build each generated/obj/*.o file in parallel

PENDING=
LNKFILES=
DONE=0
COUNT=0
CLICK=

for i in $LIBFILES click $TOYFILES
do
  [ "$i" == click ] && CLICK=1 && continue

  X=${i/lib\//lib_}
  X=${X##*/}
  OUT="generated/obj/${X%%.c}.o"
  LNKFILES="$LNKFILES $OUT"

  # $LIBFILES doesn't need to be rebuilt if newer than .config, $TOYFILES does

  [ "$OUT" -nt "$i" ] && [ -z "$CLICK" -o "$OUT" -nt "$KCONFIG_CONFIG" ] &&
    continue

  do_loudly $BUILD -c $i -o $OUT &
  PENDING="$PENDING $!"
  COUNT=$(($COUNT+1))

  # ratelimit to $CPUS many parallel jobs, detecting errors

  for j in $PENDING
  do
    [ "$COUNT" -lt "$CPUS" ] && break;

    wait $j
    DONE=$(($DONE+$?))
    COUNT=$(($COUNT-1))
    PENDING="${PENDING## $j}"
  done
  [ $DONE -ne 0 ] && break
done

# wait for all background jobs, detecting errors

for i in $PENDING
do
  wait $i
  DONE=$(($DONE+$?))
done

[ $DONE -ne 0 ] && exit 1

do_loudly $BUILD $LNKFILES $LINK || exit 1
if [ ! -z "$NOSTRIP" ] ||
  ! do_loudly ${CROSS_COMPILE}strip "$UNSTRIPPED" -o "$OUTNAME"
then
  echo "strip failed, using unstripped" && cp "$UNSTRIPPED" "$OUTNAME" ||
  exit 1
fi

# gcc 4.4's strip command is buggy, and doesn't set the executable bit on
# its output the way SUSv4 suggests it do so. While we're at it, make sure
# we don't have the "w" bit set so things like bzip2's "cp -f" install don't
# overwrite our binary through the symlink.
do_loudly chmod 555 "$OUTNAME" || exit 1

echo
