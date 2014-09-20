#!/bin/bash

[ -z "$TOPDIR" ] && TOPDIR="$(pwd)"

trap 'kill $(jobs -p) 2>/dev/null; exit 1' INT

rm -rf generated/testdir
mkdir -p generated/testdir/testdir

if [ -z "$TEST_HOST" ]
then
  if [ $# -ne 0 ]
  then
    PREFIX=generated/testdir/ scripts/single.sh "$@" || exit 1
  else
    make install_flat PREFIX=generated/testdir || exit 1
  fi
fi

cd generated/testdir
PATH="$PWD:$PATH"
cd testdir

. "$TOPDIR"/scripts/runtest.sh
[ -f "$TOPDIR/generated/config.h" ] && export OPTIONFLAGS=:$(echo $(sed -nr 's/^#define CFG_(.*) 1/\1/p' "$TOPDIR/generated/config.h") | sed 's/ /:/g')

if [ $# -ne 0 ]
then
  for i in "$@"
  do
    . "$TOPDIR"/tests/$i.test
  done
else
  for i in "$TOPDIR"/tests/*.test
  do
    CMDNAME="$(echo "$i" | sed 's@.*/\(.*\)\.test@\1@')"
    if [ -h ../$CMDNAME ] || [ ! -z "$TEST_HOST" ]
    then
      cd .. && rm -rf testdir && mkdir testdir && cd testdir || exit 1
      . $i
    else
      echo "$CMDNAME disabled"
    fi
  done
fi
