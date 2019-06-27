#!/bin/bash

source scripts/runtest.sh
source scripts/portability.sh

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
TESTDIR="$PWD"
export LC_COLLATE=C

[ -f "$TOPDIR/generated/config.h" ] &&
  export OPTIONFLAGS=:$(echo $($SED -nr 's/^#define CFG_(.*) 1/\1/p' "$TOPDIR/generated/config.h") | $SED 's/ /:/g')

do_test()
{
  cd "$TESTDIR" && rm -rf testdir && mkdir testdir && cd testdir || exit 1
  CMDNAME="${1##*/}"
  CMDNAME="${CMDNAME%.test}"
  if [ -z "$TEST_HOST" ]
  then
    C="$TESTDIR/$CMDNAME"
    [ ! -e "$C" ] && echo "$CMDNAME disabled" && return
  else
    C="$(which $CMDNAME 2>/dev/null)"
    [ -z "$C" ] && "C=$CMDNAME"
  fi

  . "$1"
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
    [ -z "$TEST_ALL" ] && [ ! -x "$i" ] && continue
    do_test "$i"
  done
fi
