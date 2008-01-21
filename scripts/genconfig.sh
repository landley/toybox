#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated

function genconfig()
{
  for i in $(echo toys/*.c | sort)
  do
    # Grab the config block for Config.in
    echo "# $i"
    sed -n '/^\*\//q;/^config [A-Z]/,$p' $i || exit 1
    echo
  done
}

genconfig > generated/Config.in
