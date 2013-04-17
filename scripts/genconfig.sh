#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated

source configure

probeconfig()
{
  # Probe for container support on target

  echo -e "# container support\nconfig TOYBOX_CONTAINER\n\tbool" || return 1
  ${CROSS_COMPILE}${CC} $CFLAGS -xc -o /dev/null - 2>/dev/null << EOF
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
    DIR="$(dirname "$j")"

    [ $(ls "$DIR" | wc -l) -lt 2 ] && continue

    echo "menu \"$(head -n 1 $j)\""
    echo

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

headerprobes()
{
  ${CROSS_COMPILE}${CC} $CFLAGS -xc -o /dev/null - 2>/dev/null << EOF
    #include <fcntl.h>
    #ifndef O_NOFOLLOW
    #error posix 2008 was a while ago now
    #endif
EOF
  if [ $? -ne 0 ]
  then
    rm -f a.out
    ${CROSS_COMPILE}${CC} $CFLAGS -xc - 2>/dev/null << EOF
      #include <stdio.h>
      #include <sys/types.h>
      #include <asm/fcntl.h>

      int main(int argc, char *argv[])
      {
        printf("0x%x\n", O_NOFOLLOW);
      }
EOF
    X=$(./a.out) 2>/dev/null
    rm -f a.out
    echo "#define O_NOFOLLOW ${X:-0}"
  fi
}

probeconfig > generated/Config.probed || rm generated/Config.probed
genconfig > generated/Config.in || rm generated/Config.in
headerprobes > generated/portability.h || rm generated/portability.h
