#!/bin/bash

# If you want to use toybox netcat to talk to a serial port, use this.

if [ ! -c "$1" ]
then
  echo "Usage: minicom.sh /dev/ttyS0"
  exit 1
fi

stty 115200 -F "$1"
stty raw -echo -ctlecho -F "$1"
stty raw -echo  # Need to do it on stdin, too.
./toybox netcat -f "$1"
stty cooked echo  # Put stdin back.
