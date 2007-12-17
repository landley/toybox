#!/bin/bash

[ -z "$TOPDIR" ] && TOPDIR="$(pwd)"

rm -rf testdir
mkdir -p testdir

if [ -z "$OLD" ]
then
  make install_flat PREFIX=testdir
fi

cd testdir
PATH=.:$PATH

. "$TOPDIR"/scripts/test/testing.sh
[ -f "$TOPDIR/gen_config.h" ] && export OPTIONFLAGS=:$(echo $(sed -nr 's/^#define CFG_(.*) 1/\1/p' "$TOPDIR/gen_config.h") | sed 's/ /:/g')

if [ $# -ne 0 ]
then
  for i in "$@"
  do
    . "$TOPDIR"/scripts/test/$i.test
  done
else
  for i in "$TOPDIR"/scripts/test/*.test
  do
    . $i
  done
fi

cd ..
rm -rf testdir
