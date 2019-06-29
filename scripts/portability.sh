# sourced to find alternate names for things

source configure

if [ -z "$(command -v "${CROSS_COMPILE}${CC}")" ]
then
  echo "No ${CROSS_COMPILE}${CC} found" >&2
  exit 1
fi

if [ -z "$SED" ]
then
  [ ! -z "$(command -v gsed 2>/dev/null)" ] && SED=gsed || SED=sed
fi
