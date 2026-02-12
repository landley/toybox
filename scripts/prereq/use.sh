#!/bin/sh

# usage: use.sh [miniconfig]

# Hermetic build using toybox-prereq, optionally using a miniconfig

if [ ! -x toybox-prereq ]
then
  echo building toybox-prereq
  scripts/prereq/build.sh || exit 1
fi

# Install prerequisites into subdir and prepend to path
rm -rf prereq && mkdir -p prereq && cp toybox-prereq prereq/toybox &&
for i in $(prereq/toybox); do ln -sf toybox prereq/$i; done &&
for i in cc as ld strip; do ln -sf $(prereq/toybox which $i) prereq/$i; done &&
export PATH="$PWD/prereq" &&

# Are we using a miniconfig?
if [ -z "$1" ]
then
  ARG=-d
else
  ARG=-n
  export KCONFIG_ALLCONFIG="$1"
fi

# Run configure and build
rm -rf generated &&
scripts/genconfig.sh $ARG &&
scripts/make.sh
