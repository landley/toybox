#!/bin/bash

# Script to build all supported cross and native compilers using
# https://github.com/richfelker/musl-cross-make
# (Check that out and run this script in that directory.)

# This isn't directly used by toybox, but is useful for testing.

# All toolchains after the first are themselves cross compiled (so they
# can be statically linked against musl on the host, for binary portability.)
# static i686 binaries are basically "poor man's x32".
BOOTSTRAP=i686-linux-musl

[ -z "$OUTPUT" ] && OUTPUT="$PWD/ccc"

if [ "$1" == clean ]
then
  rm -rf "$OUTPUT" host-* *.log
  make clean
  exit
fi

make_toolchain()
{
  # Set cross compiler path
  LP="$PATH"
  if [ -z "$TYPE" ]
  then
    OUTPUT="$PWD/host-$TARGET"
    EXTRASUB=y
  else
    if [ "$TYPE" == static ]
    then
      HOST=$BOOTSTRAP
      [ "$TARGET" = "$HOST" ] && LP="$PWD/host-$HOST/bin:$LP"
      TYPE=cross
      EXTRASUB=y
      LP="$OUTPUT/$HOST-cross/bin:$LP"
    else
      HOST="$TARGET"
      export NATIVE=y
      LP="$OUTPUT/${RENAME:-$TARGET}-cross/bin:$LP"
    fi
    COMMON_CONFIG="CC=\"$HOST-gcc -static --static\" CXX=\"$HOST-g++ -static --static\""
    export -n HOST
    OUTPUT="$OUTPUT/${RENAME:-$TARGET}-$TYPE"
  fi

  if [ -e "$OUTPUT.sqf" ] || [ -e "$OUTPUT/bin/$TARGET-ld" ] ||
     [ -e "$OUTPUT/bin/ld" ]
  then
    return
  fi

  # Change title bar to say what we're currently building

  echo === building $TARGET-$TYPE
  echo -en "\033]2;$TARGET-$TYPE\007"

  rm -rf build/"$TARGET" "$OUTPUT" &&
  if [ -z "$CPUS" ]
  then
    CPUS="$(nproc)"
    [ "$CPUS" != 1 ] && CPUS=$(($CPUS+1))
  fi
  set -x &&
  PATH="$LP" make OUTPUT="$OUTPUT" TARGET="$TARGET" \
    GCC_CONFIG="--disable-nls --disable-libquadmath --disable-decimal-float --disable-multilib --enable-languages=c,c++ $GCC_CONFIG" \
    COMMON_CONFIG="CFLAGS=\"$CFLAGS -g0 -Os\" CXXFLAGS=\"$CXXFLAGS -g0 -Os\" LDFLAGS=\"$LDFLAGS -s\" $COMMON_CONFIG" \
    install -j$CPUS || exit 1
  set +x
  echo -e '#ifndef __MUSL__\n#define __MUSL__ 1\n#endif' \
    >> "$OUTPUT/${EXTRASUB:+$TARGET/}include/features.h"

  if [ ! -z "$RENAME" ] && [ "$TYPE" == cross ]
  then
    CONTEXT="output/$RENAME-cross/bin"
    for i in "$CONTEXT/$TARGET-"*
    do
      X="$(echo $i | sed "s@.*/$TARGET-\([^-]*\)@\1@")"
      ln -sf "$TARGET-$X" "$CONTEXT/$RENAME-$X"
    done
  fi

  # Prevent cross compiler reusing dynamically linked host build files for
  # $BOOTSTRAP arch
  [ -z "$TYPE" ] && make clean

  if [ "$TYPE" == native ]
  then
    # gcc looks in "../usr/include" but not "/bin/../include" (relative to the
    # executable). That means /usr/bin/gcc looks in /usr/usr/include, so that's
    # not a fix either. So add a NOP symlink as a workaround for The Crazy.
    ln -s . "$OUTPUT/usr" || exit 1
    [ ! -z "$(which mksquashfs 2>/dev/null)" ] &&
      mksquashfs "$OUTPUT" "$OUTPUT.sqf" -all-root &&
      [ -z "$CLEANUP" ] && rm -rf "$OUTPUT"
  fi
}

# Expand compressed target into binutils/gcc "tuple" and call make_toolchain
make_tuple()
{
  PART1=${1/:*/}
  PART3=${1/*:/}
  PART2=${1:$((${#PART1}+1)):$((${#1}-${#PART3}-${#PART1}-2))}

  # Do we need to rename this toolchain after building it?
  RENAME=${PART1/*@/}
  [ "$RENAME" == "$PART1" ] && RENAME=
  PART1=${PART1/@*/}
  TARGET=${PART1}-linux-musl${PART2} 

  [ -z "$NOCLEAN" ] && rm -rf build

  for TYPE in static native
  do
    TYPE=$TYPE TARGET=$TARGET GCC_CONFIG="$PART3" RENAME="$RENAME" \
      make_toolchain 2>&1 | tee "$OUTPUT"/log/${RENAME:-$PART1}-${TYPE}.log
  done
}

mkdir -p "$OUTPUT"/log

# Make bootstrap compiler (no $TYPE, dynamically linked against host libc)
# We build the rest of the cross compilers with this so they're linked against
# musl-libc, because glibc doesn't fully support static linking and dynamic
# binaries aren't really portable between distributions
TARGET=$BOOTSTRAP make_toolchain 2>&1 | tee -a "$OUTPUT/log/$BOOTSTRAP"-host.log

if [ $# -gt 0 ]
then
  for i in "$@"
  do
    make_tuple "$i"
  done
else
  # First target builds a proper version of the $BOOTSTRAP compiler above,
  # used to build the rest (which are in alphabetical order)
  for i in i686:: \
         aarch64:eabi: armv4l:eabihf:"--with-arch=armv5t --with-float=soft" \
         armv5l:eabihf:--with-arch=armv5t armv7l:eabihf:--with-arch=armv7-a \
         "armv7m:eabi:--with-arch=armv7-m --with-mode=thumb --disable-libatomic --enable-default-pie" \
         armv7r:eabihf:"--with-arch=armv7-r --enable-default-pie" \
         i486:: m68k:: microblaze:: mips:: mips64:: mipsel:: powerpc:: \
         powerpc64:: powerpc64le:: s390x:: sh2eb:fdpic:--with-cpu=mj2 \
         sh4::--enable-incomplete-targets x86_64:: x86_64@x32:x32:
  do
    make_tuple "$i"
  done
fi
