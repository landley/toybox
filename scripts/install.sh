#!/bin/bash

# Grab default values for $CFLAGS and such.

source ./configure

# Parse command line arguments.

[ -z "$PREFIX" ] && PREFIX="."

LONG_PATH=""
while [ ! -z "$1" ]
do
  # Create symlinks instead of hardlinks?

  [ "$1" == "--symlink" ] && LINK_TYPE="-s"

  # Uninstall?

  [ "$1" == "--uninstall" ] && UNINSTALL=1

  # Delete destination command if it exists?

  [ "$1" == "--force" ] && DO_FORCE="-f"

  # Use {,usr}/{bin,sbin} paths instead of all files in one directory?

  if [ "$1" == "--long" ]
  then
    LONG_PATH="bin/"
  fi

  shift
done

echo "Compile instlist..."

$DEBUG $HOSTCC -I . scripts/install.c -o generated/instlist || exit 1
COMMANDS="$(generated/instlist $LONG_PATH)"

echo "Install commands..."

# Copy toybox itself

if [ -z "$UNINSTALL" ]
then
  mkdir -p ${PREFIX}/${LONG_PATH} || exit 1
  cp toybox ${PREFIX}/${LONG_PATH} || exit 1
else
  rm "${PREFIX}/${LONG_PATH}/toybox" 2>/dev/null
  rmdir "${PREFIX}/${LONG_PATH}" 2>/dev/null
fi
cd "${PREFIX}"

# Make links to toybox

for i in $COMMANDS
do
  # Figure out target of link

  if [ -z "$LONG_PATH" ]
  then
    DOTPATH=""
  else
    # Create subdirectory for command to go in (if necessary)

    DOTPATH="$(echo $i | sed 's@\(.*/\).*@\1@')"
    if [ -z "$UNINSTALL" ]
    then
      mkdir -p "$DOTPATH" || exit 1
    else
      rmdir "$DOTPATH" 2>/dev/null
    fi

    if [ -z "$LINK_TYPE" ]
    then
      dotpath="bin/"
    else
      if [ "$DOTPATH" != "$LONG_PATH" ]
      then
        DOTPATH="$(echo $DOTPATH | sed -e 's@[^/]*/@../@g')"$LONG_PATH
      else
        DOTPATH=""
      fi
    fi
  fi

  # Create link
  [ -z "$UNINSTALL" ] &&
    ln $DO_FORCE $LINK_TYPE ${DOTPATH}toybox $i ||
    rm $i 2>/dev/null
done
