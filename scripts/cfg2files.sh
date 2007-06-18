#!/bin/bash

# cat .config into this to get a list of .c files.

# Grab the XXX part of all CONFIG_XXX entries, removing everything after the
# second underline.  Sort the list, keep only one of each entry, convert
# to lower case, remove toybox itself from the list (as that indicates
# global symbols).

sed -nre 's/^CONFIG_(.*)=y/\1/;t skip;b;:skip;s/_.*//;p' \
	| sort -u | tr A-Z a-z | grep -v '^toybox$'
