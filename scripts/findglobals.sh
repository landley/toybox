#!/bin/bash

# Quick and dirty check to see if anybody's leaked global variables.
# We should have this, toy_list, toybuf, and toys.

nm toybox_unstripped | grep '[0-9A-Fa-f]* [BCDGRS]' | cut -d ' ' -f 3
