#!/bin/bash

# Build a standalone toybox command

if [ -z "$1" ]
then
  echo "usage: single.sh command..." >&2
  exit 1
fi

for i in "$@"
do
  NAME=$(echo $i | tr a-z- A-Z_)
  export KCONFIG_CONFIG=.singleconfig
  USET="is not set"

  make allnoconfig > /dev/null &&
  sed -i -e "s/\(CONFIG_TOYBOX\)=y/# \1 $USET/" \
         -e "s/# \(CONFIG_$NAME\) $USET/\1=y/"  \
         -e "s/# \(CONFIG_${NAME}_.*\) $USET/\1=y/" \
         -e "s/# \(CONFIG_TOYBOX_HELP.*\) $USET/\1=y/" \
         -e "s/# \(CONFIG_TOYBOX_I18N\) $USET/\1=y/" \
         -e "s/# \(CONFIG_TOYBOX_FLOAT\) $USET/\1=y/" \
         "$KCONFIG_CONFIG" &&
  make &&
  mv toybox $PREFIX$i || exit 1
done
