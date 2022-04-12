# sourced to find alternate names for things

source ./configure

if [ -z "$(command -v "${CROSS_COMPILE}${CC}")" ]
then
  echo "No ${CROSS_COMPILE}${CC} found" >&2
  exit 1
fi

if [ -z "$SED" ]
then
  [ ! -z "$(command -v gsed 2>/dev/null)" ] && SED=gsed || SED=sed
fi

# Tell linker to do dead code elimination at function level
if [ "$(uname)" == "Darwin" ]
then
  : ${LDOPTIMIZE:=-Wl,-dead_strip} ${STRIP:=strip}
else
  : ${LDOPTIMIZE:=-Wl,--gc-sections -Wl,--as-needed} ${STRIP:=strip -s -R .note* -R .comment}
fi

# Address Sanitizer
if [ ! -z "$ASAN" ]; then
  # Turn ASan on and disable most optimization to get more readable backtraces.
  # (Technically ASAN is just "-fsanitize=address" and the rest is optional.)
  ASAN_FLAGS="-fsanitize=address -O1 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls"
  CFLAGS="$CFLAGS $ASAN_FLAGS"
  HOSTCC="$HOSTCC $ASAN_FLAGS"
  NOSTRIP=1
  # Ignore leaks on exit. TODO
  export ASAN_OPTIONS="detect_leaks=0"
fi

# Centos 7 bug workaround, EOL June 30 2024.
DASHN=-n; wait -n 2>/dev/null; [ $? -eq 2 ] && unset DASHN

# If the build is using gnu tools, make them behave less randomly.
export LANG=c
export LC_ALL=C
