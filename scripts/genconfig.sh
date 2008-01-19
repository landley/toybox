#!/bin/bash

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
