#!/bin/bash

# Convenience wrapper to set $CROSS_COMPILE from short name using "ccc"
# symlink (Cross C Compiler) to a directory of cross compilers named
# $TARGET-*-cross. Tested with scripts/mcm-buildall.sh output.

# Usage: scripts/cross.sh $TARGET make distclean defconfig toybox
# With no arguments, lists available targets. Use target "all" to iterate
# through each $TARGET from the list.

CCC="$(dirname "$(readlink -f "$0")")"/../ccc
if [ ! -d "$CCC" ]
then
  echo "Create symlink 'ccc' to cross compiler directory, ala:"
  echo "  ln -s ~/musl-cross-make/ccc ccc"
  exit 1
fi

unset X Y

# Display target list?
list()
{
  ls "$CCC" | sed 's/-.*//' | sort -u | xargs
}
[ $# -eq 0 ] && list && exit

[ -z "$TOP" ] && TOP="$PWD/root/log"
mkdir -p "$TOP" || exit 1

X="$1"
shift

# build all targets?
if [ "$X" == all ]
then
  for TARGET in $(list)
  do
    LOG="$TOP/cross-log-$TARGET"
    {
      export TARGET
      echo -en "\033]2;$TARGET $*\007"

      rm -f "$LOG".{failed,success}
      "$0" $TARGET "$@" 2>&1
      X=$?
      [ $X -eq 0 -o $X -eq 42 ] && mv "$LOG".{txt,success}
    } |& tee "$LOG".txt
    [ -z "$ALL" ] && [ ! -e "$LOG".success ] &&
      { mv "$LOG".{txt,failed} ; break;}
  done

  exit
fi

# Call command with CROSS_COMPILE= as its first argument

Y=$(echo "$CCC/$X"-*cross)
Z=$(basename "$Y")
Y=$(readlink -f "$CCC"/$X-*cross)
export TARGET="${Z/-*/}"
X="$Y/bin/${Z/-cross/-}"
[ ! -e "${X}cc" ] && echo "${X}cc not found" && exit 1

CROSS_COMPILE="$X" "$@"
