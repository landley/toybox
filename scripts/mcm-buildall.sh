#!/bin/bash

# Script to build all cross and native compilers supported by musl-libc.
# This isn't directly used by toybox, but is useful for testing.

if [ ! -d litecross ]
then
  echo Run this script in musl-cross-make directory to make "ccc" directory.
  echo
  echo "  "git clone https://github.com/richfelker/musl-cross-make
  echo "  "cd musl-cross-make
  echo '  ~/toybox/scripts/mcm-buildall.sh'
  exit 1
fi

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

# Packages detect nommu via the absence of fork(). Musl provides a broken fork()
# on nommu builds that always returns -ENOSYS at runtime. Rip it out.
# (Currently only for superh/jcore.)
fix_nommu()
{
  # Rich won't merge this
  sed -i 's/--enable-fdpic$/& --enable-twoprocess/' litecross/Makefile

  PP=patches/musl-"$(sed -n 's/MUSL_VER[ \t]*=[ \t]*//p' Makefile)"
  mkdir -p "$PP" &&
  cat > "$PP"/0001-nommu.patch << 'EOF'
--- a/include/features.h
+++ b/include/features.h
@@ -3,2 +3,4 @@
 
+#define __MUSL__ 1
+
 #if defined(_ALL_SOURCE) && !defined(_GNU_SOURCE)
--- a/src/legacy/daemon.c
+++ b/src/legacy/daemon.c
@@ -17,3 +17,3 @@
 
-	switch(fork()) {
+	switch(vfork()) {
 	case 0: break;
@@ -25,3 +25,3 @@
 
-	switch(fork()) {
+	switch(vfork()) {
 	case 0: break;
--- a/src/misc/forkpty.c
+++ b/src/misc/forkpty.c
@@ -8,2 +8,3 @@
 
+#ifndef __SH_FDPIC__
 int forkpty(int *pm, char *name, const struct termios *tio, const struct winsize *ws)
@@ -57,1 +58,2 @@
 }
+#endif
--- a/src/misc/wordexp.c
+++ b/src/misc/wordexp.c
@@ -25,2 +25,3 @@
 
+#ifndef __SH_FDPIC__
 static int do_wordexp(const char *s, wordexp_t *we, int flags)
@@ -177,2 +178,3 @@
 }
+#endif
 
--- a/src/process/fork.c
+++ b/src/process/fork.c
@@ -7,2 +7,3 @@
 
+#ifndef __SH_FDPIC__
 static void dummy(int x)
@@ -37,1 +38,2 @@
 }
+#endif
--- a/Makefile
+++ b/Makefile
@@ -100,3 +100,3 @@
 	cp $< $@
-	sed -n -e s/__NR_/SYS_/p < $< >> $@
+	sed -e s/__NR_/SYS_/ < $< >> $@
 
--- a/arch/sh/bits/syscall.h.in
+++ b/arch/sh/bits/syscall.h.in
@@ -2,3 +2,5 @@
 #define __NR_exit                   1
+#ifndef __SH_FDPIC__
 #define __NR_fork                   2
+#endif
 #define __NR_read                   3
EOF

  # I won't sign the FSF's copyright assignment
  tee $(for i in patches/gcc-*; do echo $i/099-vfork.patch; done) > /dev/null << 'EOF'
--- gcc-8.3.0/fixincludes/procopen.c	2005-08-14 19:50:43.000000000 -0500
+++ gcc-bak/fixincludes/procopen.c	2020-02-06 23:27:15.408071708 -0600
@@ -116,3 +116,3 @@
    */
-  ch_id = fork ();
+  ch_id = vfork ();
   switch (ch_id)
EOF
}

fix_nommu || exit 1
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
  # Here's the list of cross compilers supported by this build script.

  # First target builds a proper version of the $BOOTSTRAP compiler above,
  # which is used to build the rest (in alphabetical order)
  for i in i686:: \
         aarch64:eabi: armv4l:eabihf:"--with-arch=armv5t --with-float=soft" \
         "armv5l:eabihf:--with-arch=armv5t --with-float=vfp" \
         "armv7l:eabihf:--with-arch=armv7-a --with-float=vfp" \
         "armv7m:eabi:--with-arch=armv7-m --with-mode=thumb --disable-libatomic --enable-default-pie" \
         armv7r:eabihf:"--with-arch=armv7-r --enable-default-pie" \
         i486:: m68k:: microblaze:: mips:: mips64:: mipsel:: powerpc:: \
         powerpc64:: powerpc64le:: s390x:: sh2eb:fdpic:--with-cpu=mj2 \
         sh4::--enable-incomplete-targets x86_64:: x86_64@x32:x32:
  do
    make_tuple "$i"
  done
fi
