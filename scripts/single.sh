#!/bin/bash

# Build a standalone toybox command

if [ -z "$1" ]
then
  echo "usage: single.sh command" >&2
  exit 1
fi

NAME=$(echo $1 | tr a-z- A-Z_)
export KCONFIG_CONFIG=.singleconfig

make allnoconfig > /dev/null &&
sed -i -e "s/\(CONFIG_TOYBOX\)=y/# \1 is not set/" \
       -e "s/# CONFIG_\($NAME\|${NAME}_[^ ]*\|TOYBOX_HELP[^ ]*\|TOYBOX_I18N\|TOYBOX_FLOAT\) is not set/CONFIG_\1=y/" \
       "$KCONFIG_CONFIG" &&
make &&
mv toybox $PREFIX$1
