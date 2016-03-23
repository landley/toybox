#!/bin/bash

# Grab default values for $CFLAGS and such.

source ./configure

[ -z "$PREFIX" ] && PREFIX="/usr/toybox"

# Parse command line arguments.

LONG_PATH=""
while [ ! -z "$1" ]
do
  # Create symlinks instead of hardlinks?
  [ "$1" == "--symlink" ] && LINK_TYPE="-s"

  # Uninstall?
  [ "$1" == "--uninstall" ] && UNINSTALL=Uninstall

  # Delete destination command if it exists?
  [ "$1" == "--force" ] && DO_FORCE="-f"

  # Use {,usr}/{bin,sbin} paths instead of all files in one directory?
  [ "$1" == "--long" ] && LONG_PATH="bin/"

  shift
done

echo "Compile instlist..."

$DEBUG $HOSTCC -I . scripts/install.c -o generated/instlist || exit 1
COMMANDS="$(generated/instlist $LONG_PATH)"

echo "${UNINSTALL:-Install} commands..."

# Copy toybox itself

if [ -z "$UNINSTALL" ]
then
  mkdir -p "${PREFIX}/${LONG_PATH}" &&
  rm -f "${PREFIX}/${LONG_PATH}/toybox" &&
  cp toybox ${PREFIX}/${LONG_PATH} || exit 1
else
  rm -f "${PREFIX}/${LONG_PATH}/toybox" 2>/dev/null
fi
cd "$PREFIX" || exit 1

# Make links to toybox

EXIT=0

for i in $COMMANDS
do
  # Figure out target of link

  if [ -z "$LONG_PATH" ]
  then
    DOTPATH=""
  else
    # Create subdirectory for command to go in (if necessary)

    DOTPATH="$(dirname "$i")"/
    if [ -z "$UNINSTALL" ]
    then
      mkdir -p "$DOTPATH" || exit 1
    fi

    if [ -z "$LINK_TYPE" ]
    then
      DOTPATH="bin/"
    else
      if [ "$DOTPATH" != "$LONG_PATH" ]
      then
        # For symlinks we need ../../bin style relative paths
        DOTPATH="$(echo $DOTPATH | sed -e 's@[^/]*/@../@g')"$LONG_PATH
      else
        DOTPATH=""
      fi
    fi
  fi

  # Create link
  if [ -z "$UNINSTALL" ]
  then
    ln $DO_FORCE $LINK_TYPE ${DOTPATH}toybox $i || EXIT=1
  else
    rm -f $i || EXIT=1
  fi
done

exit $EXIT
