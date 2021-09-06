#!/bin/bash

# Build a standalone toybox command

if [ -z "$1" ]
then
  echo "usage: single.sh command..." >&2
  exit 1
fi

# Add trailing / to PREFIX when it's set but hasn't got one
[ "$PREFIX" == "${PREFIX%/}" ] && PREFIX="${PREFIX:+$PREFIX/}"

# Harvest TOYBOX_* symbols from .config
if [ ! -e ${KCONFIG_CONFIG:=.config} ]
then
  echo "Need $KCONFIG_CONFIG for toybox global settings. Run defconfig/menuconfig." >&2
  exit 1
fi

# Force dependencies to rebuild headers if we build multiplexer after this.
touch -c "$KCONFIG_CONFIG"

SINGLE_CONFIG=.singleconfig
for i in "$@"
do
  echo -n "$i:"
  TOYFILE="$(egrep -l "TOY[(]($i)[ ,]" toys/*/*.c)"

  if [ -z "$TOYFILE" ]
  then
    echo "Unknown command '$i'" >&2
    exit 1
  fi

  KCONFIG_CONFIG="$SINGLE_CONFIG" make allnoconfig > /dev/null || exit 1

  DEPENDS=
  MPDEL=
  if [ "$i" == sh ]
  then
    DEPENDS="$(sed -n 's/USE_\([^(]*\)(NEWTOY([^,]*,.*TOYFLAG_MAYFORK.*/\1/p' toys/*/*.c)"
  else
    MPDEL='s/CONFIG_TOYBOX=y/# CONFIG_TOYBOX is not set/;t'
  fi

  # Enable stuff this command depends on
  DEPENDS="$({ echo $DEPENDS; sed -n "/^config *$i"'$/,/^$/{s/^[ \t]*depends on //;T;s/[!][A-Z0-9_]*//g;s/ *&& */|/g;p}' $TOYFILE; sed -n 's/CONFIG_\(TOYBOX_[^=]*\)=y/\1/p' "$KCONFIG_CONFIG";}| xargs | tr ' ' '|')"
  NAME=$(echo $i | tr a-z- A-Z_)
  sed -ri -e "$MPDEL" \
    -e "s/# (CONFIG_($NAME|${NAME}_.*${DEPENDS:+|$DEPENDS})) is not set/\1=y/" \
    "$SINGLE_CONFIG" || exit 1

  export OUTNAME="$PREFIX$i"
  rm -f "$OUTNAME" &&
  KCONFIG_CONFIG="$SINGLE_CONFIG" scripts/make.sh || exit 1
done
