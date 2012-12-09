#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated

source configure

probeconfig()
{
  # Probe for container support on target

  echo -e "# container support\nconfig TOYBOX_CONTAINER\n\tbool" || return 1
  ${CROSS_COMPILE}${CC} -xc -o /dev/null - 2>/dev/null << EOF
    #include <linux/sched.h>
    int x=CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET;

    int main(int argc, char *argv[]) { return unshare(x); }
EOF
  [ $? -eq 0 ] && DEFAULT=y || DEFAULT=n
  rm a.out 2>/dev/null
  echo -e "\tdefault $DEFAULT\n" || return 1
}

genconfig()
{
  # I could query the directory here, but I want to control the order
  # and capitalization in the menu
  for j in toys/*/README
  do
    echo "menu \"$(head -n 1 $j)\""
    echo

    DIR="$(dirname "$j")"

    # extract config stanzas from each source file, in alphabetical order
    for i in $(ls -1 $DIR/*.c)
    do
      # Grab the config block for Config.in
      echo "# $i"
      sed -n '/^\*\//q;/^config [A-Z]/,$p' $i || return 1
      echo
    done

    echo endmenu
  done
}

probeconfig > generated/Config.probed || rm generated/Config.probed
genconfig > generated/Config.in || rm generated/Config.in
