#!/bin/bash

# Build a standalone toybox command

if [ -z "$1" ]
then
  echo "usage: single.sh command..." >&2
  exit 1
fi

# Harvest TOYBOX_* symbols from .config
if [ ! -e .config ]
then
  echo "Need .config for toybox global settings. Run defconfig/menuconfig." >&2
  exit 1
fi

# Force dependencies to rebuild headers if we build multiplexer after this.
touch -c .config

export KCONFIG_CONFIG=.singleconfig
for i in "$@"
do
  echo -n "$i:"
  TOYFILE="$(egrep -l "TOY[(]($i)[ ,]" toys/*/*.c)"

  if [ -z "$TOYFILE" ]
  then
    echo "Unknown command '$i'" >&2
    exit 1
  fi

  # Enable stuff this command depends on
  DEPENDS="$(sed -n "/^config *$i"'$/,/^$/{s/^[ \t]*depends on //;T;s/[!][A-Z0-9_]*//g;s/ *&& */|/g;p}' $TOYFILE | xargs | tr ' ' '|')"

  NAME=$(echo $i | tr a-z- A-Z_)
  make allnoconfig > /dev/null &&
  sed -ri -e '/CONFIG_TOYBOX/d' \
    -e "s/# (CONFIG_($NAME|${NAME}_.*${DEPENDS:+|$DEPENDS})) is not set/\1=y/" \
    "$KCONFIG_CONFIG" &&
  echo "# CONFIG_TOYBOX is not set" >> "$KCONFIG_CONFIG" &&
  grep "CONFIG_TOYBOX_" .config >> "$KCONFIG_CONFIG" &&

  rm -f "$PREFIX$i" &&
  OUTNAME="$PREFIX$i" scripts/make.sh || exit 1
done
