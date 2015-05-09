#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated

source configure

probecc()
{
  ${CROSS_COMPILE}${CC} $CFLAGS -xc -o /dev/null $1 -
}

# Probe for a single config symbol with a "compiles or not" test.
# Symbol name is first argument, flags second, feed C file to stdin
probesymbol()
{
  probecc $2 2>/dev/null && DEFAULT=y || DEFAULT=n
  rm a.out 2>/dev/null
  echo -e "config $1\n\tbool" || exit 1
  echo -e "\tdefault $DEFAULT\n" || exit 1
}

probeconfig()
{
  > generated/cflags
  # llvm produces its own really stupid warnings about things that aren't wrong,
  # and although you can turn the warning off, gcc reacts badly to command line
  # arguments it doesn't understand. So probe.
  [ -z "$(probecc -Wno-string-plus-int <<< \#warn warn 2>&1 | grep string-plus-int)" ] &&
    echo -Wno-string-plus-int >> generated/cflags

  # Probe for container support on target
  probesymbol TOYBOX_CONTAINER << EOF
    #include <linux/sched.h>
    int x=CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET;

    int main(int argc, char *argv[]) { return unshare(x); }
EOF

  probesymbol TOYBOX_FIFREEZE -c << EOF
    #include <linux/fs.h>
    #ifndef FIFREEZE
    #error nope
    #endif
EOF

  # Work around some uClibc limitations
  probesymbol TOYBOX_ICONV -c << EOF
    #include "iconv.h"
EOF
  probesymbol TOYBOX_FALLOCATE << EOF
    #include <fcntl.h>

    int main(int argc, char *argv[]) { return posix_fallocate(0,0,0); }
EOF
  
  # Android and some other platforms miss utmpx
  probesymbol TOYBOX_UTMPX -c << EOF
    #include <utmpx.h>
    #ifndef BOOT_TIME
    #error nope
    #endif
    int main(int argc, char *argv[]) {
      struct utmpx *a; 
      if (0 != (a = getutxent())) return 0;
      return 1;
    }
EOF

  # Android is missing shadow.h
  probesymbol TOYBOX_SHADOW -c << EOF
    #include <shadow.h>
    int main(int argc, char *argv[]) {
      struct spwd *a = getspnam("root"); return 0;
    }
EOF

  # Some commands are android-specific
  probesymbol TOYBOX_ON_ANDROID -c << EOF
    #ifndef __ANDROID__
    #error nope
    #endif
EOF
}

genconfig()
{
  # Reverse sort puts posix first, examples last.
  for j in $(ls toys/*/README | sort -r)
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

probeconfig > generated/Config.probed || rm generated/Config.probed
genconfig > generated/Config.in || rm generated/Config.in
