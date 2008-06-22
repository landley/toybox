#!/bin/bash

[ -z "$TOPDIR" ] && TOPDIR="$(pwd)"

rm -rf testdir
mkdir -p testdir

if [ -z "$TEST_HOST" ]
then
  make install_flat PREFIX=testdir || exit 1
fi

cd testdir
PATH=.:$PATH

. "$TOPDIR"/scripts/test/testing.sh
[ -f "$TOPDIR/generated/config.h" ] && export OPTIONFLAGS=:$(echo $(sed -nr 's/^#define CFG_(.*) 1/\1/p' "$TOPDIR/generated/config.h") | sed 's/ /:/g')

if [ $# -ne 0 ]
then
  for i in "$@"
  do
    . "$TOPDIR"/scripts/test/$i.test
  done
else
  for i in "$TOPDIR"/scripts/test/*.test
  do
    CMDNAME="$(echo "$i" | sed 's@.*/\(.*\)\.test@\1@')"
    if [ -h $CMDNAME ] || [ ! -z "$TEST_HOST" ]
    then
      . $i
    else
      echo "$CMDNAME disabled"
    fi
  done
fi
