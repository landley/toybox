#!/bin/bash

# Grab default values for $CFLAGS and such.
set -o pipefail
source scripts/portability.sh
mkdir -p "$UNSTRIPPED"

# Respond to V= by echoing command lines as well as running them
unset DOTPROG
do_loudly()
{
  [ -n "$V" ] && echo "$@" || echo -n "$DOTPROG"
  "$@"
}

# Is anything under directory $2 newer than generated/$1 (or does it not exist)?
isnewer()
{
  find "${@:2}" -newer "$GENDIR/$1" &>/dev/null && return 1
  [ "$((DIDNEWER++))" -eq 0 ] && echo -n "$GENDIR/{" || echo -n ,
  echo -n $1
  return 0
}

# Build a tool that runs on the host
hostcomp()
{
  if [ ! -f "$UNSTRIPPED"/$1 ] || [ "$UNSTRIPPED"/$1 -ot scripts/$1.c ]
  then
    do_loudly $HOSTCC scripts/$1.c -o "$UNSTRIPPED"/$1 || exit 1
  fi
}

# Used once for dependency checking, then again to write build.sh
genbuildsh()
{
  LLINK="$(echo $LDOPTIMIZE $LDFLAGS $(cat "$GENDIR"/optlibs.dat))"
  echo -e "#!/bin/sh\n\nPATH='$PATH'\nBUILD='$BUILD'\nLINK='$LLINK'\n"
  echo -e "\$BUILD lib/*.c $TOYFILES \$LINK -o $OUTNAME"
}

# Extract a list of toys/*/*.c files to compile from the data in $KCONFIG_CONFIG
# (First command names, then filenames with relevant {NEW,OLD}TOY() macro.)

[ -n "$V" ] && echo -e "\nWhich C files to build..."
[ -d ".git" ] && [ -n "$(which git 2>/dev/null)" ] &&
   GITHASH="-DTOYBOX_VERSION=\"$(git describe --tags --abbrev=12 2>/dev/null)\""
TOYFILES="$($SED -n 's/^CONFIG_\([^=]*\)=.*/\1/p' "$KCONFIG_CONFIG" | xargs | tr ' [A-Z]' '|[a-z]')"
TOYFILES="main.c $(egrep -l "TOY[(]($TOYFILES)[ ,]" toys/*/*.c | xargs)"
BUILD="$(echo ${CROSS_COMPILE}${CC} $CFLAGS -I . $OPTIMIZE $GITHASH)"

if [ "${TOYFILES/pending//}" != "$TOYFILES" ]
then
  echo -e "\n\033[1;31mwarning: using unfinished code from toys/pending\033[0m"
fi

# Probe library list if our compiler/linker options changed
if ! cmp -s <(genbuildsh 2>/dev/null | head -n 5) \
            <(head -n 5 "$GENDIR"/build.sh 2>/dev/null)
then
  echo -n "Library probe"

  # --as-needed removes libraries we don't use any symbols out of, but the
  # compiler has no way to ignore a library that doesn't exist, so detect
  # and skip nonexistent libraries for it.

  > "$GENDIR"/optlibs.dat
  for i in util crypt m resolv selinux smack attr crypto z log iconv tls ssl
  do
    ${CROSS_COMPILE}${CC} $CFLAGS $LDFLAGS -xc - -o "$UNSTRIPPED"/libprobe -l$i\
      &>/dev/null <<<"int main(int argc, char *argv[]) {return 0;}" &&
      echo -l$i >> "$GENDIR"/optlibs.dat
    echo -n .
  done
  rm -f "$UNSTRIPPED"/libprobe
  echo
fi

# Write a canned build line for use on crippled build machines.
genbuildsh > "$GENDIR"/build.sh && chmod +x "$GENDIR"/build.sh || exit 1

if isnewer Config.in toys || isnewer Config.in Config.in
then
  scripts/genconfig.sh
fi

# Create a list of all the commands toybox can provide.
if isnewer newtoys.h toys
then
  # The multiplexer is the first element in the array
  echo "USE_TOYBOX(NEWTOY(toybox, 0, TOYFLAG_STAYROOT|TOYFLAG_NOHELP))" \
    > "$GENDIR"/newtoys.h
  # Sort rest by name for binary search (copy name to front, sort, remove copy)
  $SED -n 's/^\(USE_[^(]*(.*TOY(\)\([^,]*\)\(,.*\)/\2 \1\2\3/p' toys/*/*.c \
    | sort -s -k 1,1 | $SED 's/[^ ]* //'  >> "$GENDIR"/newtoys.h
  [ $? -ne 0 ] && exit 1
fi

#TODO: "make $SED && make" doesn't regenerate config.h because diff .config
if true #isnewer config.h "$KCONFIG_CONFIG"
then
  # This long and roundabout sed invocation is to make old versions of sed
  # happy. New ones have '\n' so can replace one line with two without all
  # the branches and tedious mucking about with hyperspace.
  # TODO: clean this up to use modern stuff.

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
    $KCONFIG_CONFIG > "$GENDIR"/config.h || exit 1
fi

# Process config.h and newtoys.h to generate FLAG_x macros. Note we must
# always #define the relevant macro, even when it's disabled, because we
# allow multiple NEWTOY() in the same C file. (When disabled the FLAG is 0,
# so flags&0 becomes a constant 0 allowing dead code elimination.)

hostcomp mkflags
if isnewer flags.h toys "$KCONFIG_CONFIG"
then
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
      cat "$GENDIR"/config.h
    else
      $SED '/USE_.*([^)]*)$/s/$/ __VA_ARGS__/' "$GENDIR"/config.h
    fi
    echo '#include "lib/toyflags.h"'
    cat "$GENDIR"/newtoys.h

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
    tee "$GENDIR"/flags.raw | "$UNSTRIPPED"/mkflags > "$GENDIR"/flags.h || exit 1
fi

# Extract global structure definitions and flag definitions from toys/*/*.c

function getglobals()
{
  for i in toys/*/*.c
  do
    # alas basename -s isn't in posix yet.
    NAME="$(echo $i | $SED 's@.*/\(.*\)\.c@\1@')"
    DATA="$($SED -n -e '/^GLOBALS(/,/^)/b got;b;:got' \
            -e 's/^GLOBALS(/_data {/' \
            -e 's/^)/};/' -e 'p' $i)"
    [ -n "$DATA" ] && echo -e "// $i\n\nstruct $NAME$DATA\n"
  done
}

if isnewer globals.h toys
then
  GLOBSTRUCT="$(getglobals)"
  (
    echo "$GLOBSTRUCT"
    echo
    echo "extern union global_union {"
    echo "$GLOBSTRUCT" | \
      $SED -n 's/struct \(.*\)_data {/	struct \1_data \1;/p'
    echo "} this;"
  ) > "$GENDIR"/globals.h
fi

hostcomp mktags
if isnewer tags.h toys
then
  $SED -n '/TAGGED_ARRAY(/,/^)/{s/.*TAGGED_ARRAY[(]\([^,]*\),/\1/;p}' \
    toys/*/*.c lib/*.c | "$UNSTRIPPED"/mktags > "$GENDIR"/tags.h
fi

hostcomp config2help
if isnewer help.h "$GENDIR"/Config.in
then
  "$UNSTRIPPED"/config2help Config.in $KCONFIG_CONFIG > "$GENDIR"/help.h || exit 1
fi
[ -z "$DIDNEWER" ] || echo }

[ -n "$NOBUILD" ] && exit 0

echo -n "Compile $OUTNAME"
[ -n "$V" ] && echo
DOTPROG=.

# This is a parallel version of: do_loudly $BUILD $FILES $LLINK || exit 1

# Any headers newer than the oldest generated/obj file?
X="$(ls -1t "$GENDIR"/obj/* 2>/dev/null | tail -n 1)"
# TODO: redo this
if [ ! -e "$X" ] || [ -n "$(find toys -name "*.h" -newer "$X")" ]
then
  rm -rf "$GENDIR"/obj && mkdir -p "$GENDIR"/obj || exit 1
else
  rm -f "$GENDIR"/obj/main.o || exit 1
fi

# build each generated/obj/*.o file in parallel

unset PENDING LNKFILES CLICK
DONE=0
COUNT=0

for i in lib/*.c click $TOYFILES
do
  [ "$i" == click ] && CLICK=1 && continue

  X=${i/lib\//lib_}
  X=${X##*/}
  OUT="$GENDIR/obj/${X%%.c}.o"
  LNKFILES="$LNKFILES $OUT"

  # Library files don't get rebuilt if older than .config, commands do.
  [ "$OUT" -nt "$i" ] && [ -z "$CLICK" -o "$OUT" -nt "$KCONFIG_CONFIG" ] &&
    continue

  do_loudly $BUILD -c $i -o $OUT &

  # ratelimit to $CPUS many parallel jobs, detecting errors
  [ $((++COUNT)) -ge $CPUS ] && { wait $DASHN; DONE=$?; : $((--COUNT)); }
  [ $DONE -ne 0 ] && break
done

# wait for all background jobs, detecting errors
while [ $((COUNT--)) -gt 0 ]
do
  wait $DASHN;
  DONE=$((DONE+$?))
done
[ $DONE -ne 0 ] && exit 1

UNSTRIPPED="$UNSTRIPPED/${OUTNAME/*\//}"
do_loudly $BUILD $LNKFILES $LLINK -o "$UNSTRIPPED" || exit 1
if [ -n "$NOSTRIP" ] ||
  ! do_loudly ${CROSS_COMPILE}${STRIP} "$UNSTRIPPED" -o "$OUTNAME"
then
  [ -z "$NOSTRIP" ] && echo "strip failed, using unstripped"
  rm -f "$OUTNAME" &&
  cp "$UNSTRIPPED" "$OUTNAME" || exit 1
fi

# gcc 4.4's strip command is buggy, and doesn't set the executable bit on
# its output the way SUSv4 suggests it do so. While we're at it, make sure
# we don't have the "w" bit set so things like bzip2's "cp -f" install don't
# overwrite our binary through the symlink.
do_loudly chmod 555 "$OUTNAME" || exit 1

echo
