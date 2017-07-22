#!/bin/bash

TOPDIR="$PWD"
FILES="$PWD"/tests/files

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
export LC_COLLATE=C

. "$TOPDIR"/scripts/runtest.sh
[ -f "$TOPDIR/generated/config.h" ] && export OPTIONFLAGS=:$(echo $(sed -nr 's/^#define CFG_(.*) 1/\1/p' "$TOPDIR/generated/config.h") | sed 's/ /:/g')

do_test()
{
  CMDNAME="${1##*/}"
  CMDNAME="${CMDNAME%.test}"
  if [ -z "$TEST_HOST" ]
  then
    [ -z "$2" ] && C="$(readlink -f ../$CMDNAME)" || C="$(which $CMDNAME)"
  else
    C="$CMDNAME"
  fi
  if [ ! -z "$C" ]
  then
    . "$1"
  else
    echo "$CMDNAME disabled"
  fi
}

if [ $# -ne 0 ]
then
  for i in "$@"
  do
    do_test "$TOPDIR"/tests/$i.test
  done
else
  for i in "$TOPDIR"/tests/*.test
  do
    if [ -z "$TEST_HOST" ]
    then
      do_test "$i" 1
    else
      rm -rf testdir && mkdir testdir && cd testdir || exit 1
      do_test "$i"
      cd ..
    fi
  done
fi
