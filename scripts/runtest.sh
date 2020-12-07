# Simple test harness infrastructure
#
# Copyright 2005 by Rob Landley

# This file defines two main functions, "testcmd" and "optional". The
# first performs a test, the second enables/disables tests based on
# configuration options.

# The following environment variables enable optional behavior in "testing":
#    DEBUG - Show every command run by test script.
#    VERBOSE - "all" continue after failed test
#              "quiet" like all but just print FAIL (no diff -u).
#              "nopass" don't show successful tests
#
# The "testcmd" function takes five arguments:
#	$1) Description to display when running command
#	$2) Command line arguments to command
#	$3) Expected result (on stdout)
#	$4) Data written to file "input"
#	$5) Data written to stdin
#
# The "testing" function is like testcmd but takes a complete command line
# (I.E. you have to include the command name.) The variable $C is an absolute
# path to the command being tested, which can bypass shell builtins.
#
# The exit value of testcmd is the exit value of the command it ran.
#
# The environment variable "FAILCOUNT" contains a cumulative total of the
# number of failed tests.
#
# The "optional" function is used to skip certain tests (by setting the
# environment variable SKIP), ala:
#   optional CFG_THINGY
#
# The "optional" function checks the environment variable "OPTIONFLAGS",
# which is either empty (in which case it always clears SKIP) or
# else contains a colon-separated list of features (in which case the function
# clears SKIP if the flag was found, or sets it to 1 if the flag was not found).

export FAILCOUNT=0
export SKIP=

# Helper functions

# Check config to see if option is enabled, set SKIP if not.

SHOWPASS=PASS
SHOWFAIL=FAIL
SHOWSKIP=SKIP

if tty -s <&1
then
  SHOWPASS="$(echo -e "\033[1;32m${SHOWPASS}\033[0m")"
  SHOWFAIL="$(echo -e "\033[1;31m${SHOWFAIL}\033[0m")"
  SHOWSKIP="$(echo -e "\033[1;33m${SHOWSKIP}\033[0m")"
fi

optional()
{
  option=`printf %s "$OPTIONFLAGS" | egrep "(^|:)$1(:|\$)"`
  # Not set?
  if [ -z "$1" ] || [ -z "$OPTIONFLAGS" ] || [ ${#option} -ne 0 ]
  then
    SKIP=""
    return
  fi
  SKIP=1
}

skipnot()
{
  if [ "$VERBOSE" == quiet ]
  then
    eval "$@" 2>/dev/null
  else
    eval "$@"
  fi
  [ $? -eq 0 ] || SKIPNEXT=1
}

toyonly()
{
  IS_TOYBOX="$("$C" --version 2>/dev/null)"
  # Ideally we'd just check for "toybox", but toybox sed lies to make autoconf
  # happy, so we have at least two things to check for.
  case "$IS_TOYBOX" in
    toybox*) ;;
    This\ is\ not\ GNU*) ;;
    *) SKIPNEXT=1 ;;
  esac

  "$@"
}

wrong_args()
{
  if [ $# -ne 5 ]
  then
    printf "%s\n" "Test $NAME has the wrong number of arguments ($# $*)" >&2
    exit
  fi
}

# Announce success
do_pass()
{
  [ "$VERBOSE" != "nopass" ] && printf "%s\n" "$SHOWPASS: $NAME"
}

# The testing function

testing()
{
  NAME="$CMDNAME $1"
  wrong_args "$@"

  [ -z "$1" ] && NAME=$2

  [ -n "$DEBUG" ] && set -x

  if [ -n "$SKIP" -o -n "$SKIP_HOST" -a -n "$TEST_HOST" -o -n "$SKIPNEXT" ]
  then
    [ "$VERBOSE" != quiet ] && printf "%s\n" "$SHOWSKIP: $NAME"
    unset SKIPNEXT
    return 0
  fi

  echo -ne "$3" > expected
  [ ! -z "$4" ] && echo -ne "$4" > input || rm -f input
  echo -ne "$5" | ${EVAL:-eval --} "$2" > actual
  RETVAL=$?

  # Catch segfaults
  [ $RETVAL -gt 128 ] && [ $RETVAL -lt 255 ] &&
    echo "exited with signal (or returned $RETVAL)" >> actual
  DIFF="$(diff -au${NOSPACE:+w} expected actual)"
  if [ ! -z "$DIFF" ]
  then
    FAILCOUNT=$(($FAILCOUNT+1))
    printf "%s\n" "$SHOWFAIL: $NAME"
    if [ "$VERBOSE" != quiet ]
    then
      [ ! -z "$4" ] && printf "%s\n" "echo -ne \"$4\" > input"
      printf "%s\n" "echo -ne '$5' |$EVAL $2"
      printf "%s\n" "$DIFF"
      [ "$VERBOSE" != all ] && exit 1
    fi
  else
    [ "$VERBOSE" != "nopass" ] && printf "%s\n" "$SHOWPASS: $NAME"
  fi
  rm -f input expected actual

  [ -n "$DEBUG" ] && set +x

  return 0
}

testcmd()
{
  wrong_args "$@"

  X="$1"
  [ -z "$X" ] && X="$CMDNAME $2"
  testing "$X" "\"$C\" $2" "$3" "$4" "$5"
}

# Announce failure and handle fallout for txpect
do_fail()
{
  FAILCOUNT=$(($FAILCOUNT+1))
  printf "%s\n" "$SHOWFAIL: $NAME"
  if [ ! -z "$CASE" ]
  then
    echo "Expected '$CASE'"
    echo "Got '$A'"
  fi
  [ "$VERBOSE" != all ] && [ "$VERBOSE" != quiet ] && exit 1
}

# txpect NAME COMMAND [I/O/E/Xstring]...
# Run COMMAND and interact with it: send I strings to input, read O or E
# strings from stdout or stderr (empty string is "read line of input here"),
# X means close stdin/stdout/stderr and match return code (blank means nonzero)
txpect()
{
  # Run command with redirection through fifos
  NAME="$CMDNAME $1"
  CASE=
  VERBOSITY=

  if [ $# -lt 2 ] || ! mkfifo in-$$ out-$$ err-$$
  then
    do_fail
    return
  fi
  eval "$2" <in-$$ >out-$$ 2>err-$$ &
  shift 2
  : {IN}>in-$$ {OUT}<out-$$ {ERR}<err-$$ && rm in-$$ out-$$ err-$$

  [ $? -ne 0 ] && { do_fail;return;}

  # Loop through challenge/response pairs, with 2 second timeout
  while [ $# -gt 0 ]
  do
    VERBOSITY="$VERBOSITY"$'\n'"$1"
    LEN=$((${#1}-1))
    CASE="$1"
    A=
    case ${1::1} in

      # send input to child
      I) printf %s "${1:1}" >&$IN || { do_fail;break;} ;;

      # check output from child
      [OE])
        [ $LEN == 0 ] && LARG="" || LARG="-rN $LEN"
        O=$OUT
        [ ${1::1} == 'E' ] && O=$ERR
        A=
        read -t2 $LARG A <&$O
        VERBOSITY="$VERBOSITY"$'\n'"$A"
        if [ $LEN -eq 0 ]
        then
          [ -z "$A" ] && { do_fail;break;}
        else
          if [ "$A" != "${1:1}" ]
          then
            # Append the rest of the output if there is any.
            read -t.1 B <&$O
            A="$A$B"
            read -t.1 -rN 9999 B<&$ERR
            do_fail;break;
          fi
        fi
        ;;

      # close I/O and wait for exit
      X)
        exec {IN}<&- {OUT}<&- {ERR}<&-
        wait
        A=$?
        if [ -z "$LEN" ]
        then
          [ $A -eq 0 ] && { do_fail;break;}        # any error
        else
          [ $A != "${1:1}" ] && { do_fail;break;}  # specific value
        fi
        ;;
      *) do_fail; break ;;
    esac
    shift
  done
  # In case we already closed it
  exec {IN}<&- {OUT}<&- {ERR}<&-

  if [ $# -eq 0 ]
  then
    do_pass
  else
    [ "$VERBOSE" != quiet ] && echo "$VERBOSITY" >&2
  fi
}

# Recursively grab an executable and all the libraries needed to run it.
# Source paths beginning with / will be copied into destpath, otherwise
# the file is assumed to already be there and only its library dependencies
# are copied.

mkchroot()
{
  [ $# -lt 2 ] && return

  echo -n .

  dest=$1
  shift
  for i in "$@"
  do
    [ "${i:0:1}" == "/" ] || i=$(which $i)
    [ -f "$dest/$i" ] && continue
    if [ -e "$i" ]
    then
      d=`echo "$i" | grep -o '.*/'` &&
      mkdir -p "$dest/$d" &&
      cat "$i" > "$dest/$i" &&
      chmod +x "$dest/$i"
    else
      echo "Not found: $i"
    fi
    mkchroot "$dest" $(ldd "$i" | egrep -o '/.* ')
  done
}

# Set up a chroot environment and run commands within it.
# Needed commands listed on command line
# Script fed to stdin.

dochroot()
{
  mkdir tmpdir4chroot
  mount -t ramfs tmpdir4chroot tmpdir4chroot
  mkdir -p tmpdir4chroot/{etc,sys,proc,tmp,dev}
  cp -L testing.sh tmpdir4chroot

  # Copy utilities from command line arguments

  echo -n "Setup chroot"
  mkchroot tmpdir4chroot $*
  echo

  mknod tmpdir4chroot/dev/tty c 5 0
  mknod tmpdir4chroot/dev/null c 1 3
  mknod tmpdir4chroot/dev/zero c 1 5

  # Copy script from stdin

  cat > tmpdir4chroot/test.sh
  chmod +x tmpdir4chroot/test.sh
  chroot tmpdir4chroot /test.sh
  umount -l tmpdir4chroot
  rmdir tmpdir4chroot
}
