#!/bin/bash

# Grab default values for $CFLAGS and such.

source ./configure

echo "Extract configuration information from toys/*.c files..."
scripts/genconfig.sh

echo "Generate headers from toys/*.h..."

# Create a list of all the applets toybox can provide.  Note that the first
# entry is out of order on purpose (the toybox multiplexer applet must be the
# first element of the array).  The rest must be sorted in alphabetical order
# for fast binary search.

function newtoys()
{
  for i in toys/*.c
  do
    sed -n -e '1,/^config [A-Z]/s/^USE_/&/p' $i || exit 1
  done
}
echo "NEWTOY(toybox, NULL, 0)" > generated/newtoys.h
newtoys | sed 's/\(.*TOY(\)\([^,]*\),\(.*\)/\2 \1\2,\3/' | sort -k 1,1 \
	| sed 's/[^ ]* //'  >> generated/newtoys.h

# Extract global structure definitions from toys/*.c

function getglobals()
{
  for i in toys/*.c
  do
    NAME="$(echo $i | sed 's@toys/\(.*\)\.c@\1@')"

    echo -e "// $i\n"
    sed -n -e '/^DEFINE_GLOBALS(/,/^)/b got;b;:got' \
        -e 's/^DEFINE_GLOBALS(/struct '"$NAME"'_data {/' \
        -e 's/^)/};/' -e 'p' $i
  done
}

GLOBSTRUCT="$(getglobals)"
(
  echo "$GLOBSTRUCT"
  echo
  echo "extern union global_union {"
  echo "$GLOBSTRUCT" | sed -n 's/struct \(.*\)_data {/	struct \1_data \1;/p'
  echo "} this;"
) > generated/globals.h

# Only recreate generated/help.h if python is installed
if [ ! -z "$(which python)" ] && [ ! -z "$(grep 'CONFIG_HELP=y' .config)" ]
then
  echo "Extract help text from Config.in."
  scripts/config2help.py Config.in > generated/help.h || exit 1
fi

echo "Make generated/config.h from .config."

# This long and roundabout sed invocation is to make old versions of sed happy.
# New ones have '\n' so can replace one line with two without all the branches
# and tedious mucking about with hold space.

sed -n \
  -e 's/^# CONFIG_\(.*\) is not set.*/\1/' \
  -e 't notset' \
  -e 's/^CONFIG_\(.*\)=y.*/\1/' \
  -e 't isset' \
  -e 's/^CONFIG_\([^=]*\)=\(.*\)/#define CFG_\1 \2/p' \
  -e 'd' \
  -e ':notset' \
  -e 'h' \
  -e 's/.*/#define CFG_& 0/p' \
  -e 'g' \
  -e 's/.*/#define USE_&(...)/p' \
  -e 'd' \
  -e ':isset' \
  -e 'h' \
  -e 's/.*/#define CFG_& 1/p' \
  -e 'g' \
  -e 's/.*/#define USE_&(...) __VA_ARGS__/p' \
  .config > generated/config.h || exit 1

# Extract a list of toys/*.c files to compile from the data in ".config" with
# sed, sort, and tr:

# 1) Grab the XXX part of all CONFIG_XXX entries, removing everything after the
# second underline
# 2) Sort the list, keeping only one of each entry.
# 3) Convert to lower case.
# 4) Remove toybox itself from the list (as that indicates global symbols).
# 5) Add "toys/" prefix and ".c" suffix.

TOYFILES=$(cat .config | sed -nre 's/^CONFIG_(.*)=y/\1/;t skip;b;:skip;s/_.*//;p' | sort -u | tr A-Z a-z | grep -v '^toybox$' | sed 's@\(.*\)@toys/\1.c@' )

echo "Compile toybox..."

$DEBUG $CC $CFLAGS -I . -o toybox_unstripped $OPTIMIZE main.c lib/*.c \
  $TOYFILES -Wl,--as-needed,-lutil,--no-as-needed || exit 1
$DEBUG $STRIP toybox_unstripped -o toybox || exit 1
