#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated
OUTFILE=generated/Config.in

source configure

probeconfig()
{
  # Probe for container support on target

  echo -e "# container support\nconfig TOYBOX_CONTAINER\n\tbool" || return 1
  ${CROSS_COMPILE}${CC} -c -xc -o /dev/null - 2>/dev/null << EOF
    #include <linux/sched.h>
    int x=CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET;
EOF
  [ $? -eq 0 ] && DEFAULT=y || DEFAULT=n
  echo -e "\tdefault $DEFAULT\n" || return 1
}
genconfig()
{
  # extract config stanzas from each command source file, in alphabetical order

  for i in $(ls -1 toys/*.c)
  do
    # Grab the config block for Config.in
    echo "# $i"
    sed -n '/^\*\//q;/^config [A-Z]/,$p' $i || return 1
    echo
  done
}

probeconfig > generated/Config.probed || rm generated/Config.probed
genconfig > generated/Config.in || rm generated/Config.in
