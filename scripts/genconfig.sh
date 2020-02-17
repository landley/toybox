#!/bin/bash

# This has to be a separate file from scripts/make.sh so it can be called
# before menuconfig.  (It's called again from scripts/make.sh just to be sure.)

mkdir -p generated

source scripts/portability.sh

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
    #include <stdio.h>
    #include <sys/syscall.h>
    #include <linux/sched.h>
    int x=CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET;

    int main(int argc, char *argv[]){printf("%d", x+SYS_unshare+ SYS_setns);}
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

  probesymbol TOYBOX_ANDROID_SCHEDPOLICY << EOF
    #include <processgroup/sched_policy.h>

    int main(int argc,char *argv[]) { get_sched_policy_name(0); }
EOF

  # nommu support
  probesymbol TOYBOX_FORK << EOF
    #include <unistd.h>
    int main(int argc, char *argv[]) { return fork(); }
EOF
  echo -e '\tdepends on !TOYBOX_FORCE_NOMMU'

  probesymbol TOYBOX_PRLIMIT << EOF
    #include <sys/types.h>
    #include <sys/time.h>
    #include <sys/resource.h>
    int prlimit(pid_t pid, int resource, const struct rlimit *new_limit,
      struct rlimit *old_limit);
    int main(int argc, char *argv[]) { prlimit(0, 0, 0, 0); }
EOF

  probesymbol TOYBOX_GETRANDOM << EOF
    #include <sys/random.h>
    int main(void) { char buf[100]; getrandom(buf, 100, 0); }
EOF
}

genconfig()
{
  # Reverse sort puts posix first, examples last.
  for j in $(ls toys/*/README | sort -s -r)
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
      $SED -n '/^\*\//q;/^config [A-Z]/,$p' $i || return 1
      echo
    done

    echo endmenu
  done
}

probeconfig > generated/Config.probed || rm generated/Config.probed
genconfig > generated/Config.in || rm generated/Config.in

# Find names of commands that can be built standalone in these C files
toys()
{
  grep 'TOY(.*)' "$@" | grep -v TOYFLAG_NOFORK | grep -v "0))" | \
    $SED -En 's/([^:]*):.*(OLD|NEW)TOY\( *([a-zA-Z][^,]*) *,.*/\1:\3/p'
}

WORKING=
PENDING=
toys toys/*/*.c | (
while IFS=":" read FILE NAME
do
  [ "$NAME" == help ] && continue
  [ "$NAME" == install ] && continue
  echo -e "$NAME: $FILE *.[ch] lib/*.[ch]\n\tscripts/single.sh $NAME\n"
  echo -e "test_$NAME:\n\tscripts/test.sh $NAME\n"
  [ "${FILE/pending//}" != "$FILE" ] &&
    PENDING="$PENDING $NAME" ||
    WORKING="$WORKING $NAME"
done &&
echo -e "clean::\n\t@rm -f $WORKING $PENDING" &&
echo -e "list:\n\t@echo $(echo $WORKING | tr ' ' '\n' | sort | xargs)" &&
echo -e "list_pending:\n\t@echo $(echo $PENDING | tr ' ' '\n' | sort | xargs)" &&
echo -e ".PHONY: $WORKING $PENDING" | $SED 's/ \([^ ]\)/ test_\1/g'
) > .singlemake
