#!/bin/bash

# Build a standalone toybox command

if [ -z "$1" ]
then
  echo "usage: single.sh command..." >&2
  exit 1
fi

for i in "$@"
do

  TOYFILE="$(egrep -l "TOY[(]($i)[ ,]" toys/*/*.c)"

  if [ -z "$TOYFILE" ]
  then
    echo "Unknown command '$i'" >&2
    exit 1
  fi

  DEPENDS="$(sed -n 's/^[ \t]*depends on //;T;s/[!][A-Z0-9_]*//g;s/ *&& */|/g;p' $TOYFILE | grep -v SMACK | xargs | tr ' ' '|')"

  NAME=$(echo $i | tr a-z- A-Z_)
  export KCONFIG_CONFIG=.singleconfig

  make allnoconfig > /dev/null &&
  sed -ri -e "s/CONFIG_TOYBOX=y/# CONFIG_TOYBOX is not set/;t" \
    -e "s/# (CONFIG_(TOYBOX(|_HELP.*|_I18N|_FLOAT)|$NAME|${NAME}_.*${DEPENDS:+|$DEPENDS})) is not set/\1=y/" \
    "$KCONFIG_CONFIG" &&
  make &&
  mv toybox $PREFIX$i || exit 1
done
