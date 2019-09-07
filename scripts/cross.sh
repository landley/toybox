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
  echo "Create symlink 'ccc' to cross compiler directory"
  exit 1
fi

unset X Y

# Display target list?
list()
{
  ls "$CCC" | sed 's/-.*//' | sort -u | xargs
}
[ $# -eq 0 ] && list && exit

X="$1"
shift

# build all targets?
if [ "$X" == all ]
then
  for TARGET in $(list)
  do
    mkdir -p output/$TARGET
    {
      export TARGET
      "$0" $TARGET "$@" 2>&1 || mv output/$TARGET{,.failed}
    } | tee output/$TARGET/log.txt
  done

  exit
fi

# Call command with CROSS_COMPILE= as its first argument

Y=$(readlink -f "$CCC"/$X-*cross)
X=$(basename "$Y")
export TARGET="${X/-*/}"
X="$Y/bin/${X/-cross/-}"
[ ! -e "${X}cc" ] && echo "${X}cc not found" && exit 1

CROSS_COMPILE="$X" "$@"
