#!/bin/bash

# Quick and dirty check to see if anybody's leaked global variables.
# We should have this, toy_list, toybuf, and toys.

nm --size-sort generated/unstripped/toybox | grep '[0-9A-Fa-f]* [BCDGRS]' #| cut -d ' ' -f 3
