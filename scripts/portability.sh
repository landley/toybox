# sourced to find alternate names for things

if [ -z "$SED" ]
then
  [ ! -z "$(which gsed 2>/dev/null)" ] && SED=gsed || SED=sed
fi
