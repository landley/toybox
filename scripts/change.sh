#!/bin/sh

# build each command as a standalone executable

NOBUILD=1 scripts/make.sh > /dev/null &&
${HOSTCC:-cc} -I . scripts/install.c -o generated/instlist &&
export PREFIX=${PREFIX:-change/} &&
mkdir -p "$PREFIX" || exit 1

# Build all the commands standalone except:

# sh - shell builtins like "cd" and "exit" need the multiplexer
# help - needs to know what other commands are enabled (use command --help)

for i in $(generated/instlist | egrep -vw "sh|help")
do
  printf ' %s' "$i" &&
  scripts/single.sh $i > /dev/null 2>$PREFIX/${i}.bad &&
    rm $PREFIX/${i}.bad || printf '*'
done
echo
