# Makefile for toybox.
# Copyright 2006 Rob Landley <rob@landley.net>

CFLAGS  = -Wall -Os -s
CC      = $(CROSS_COMPILE)gcc $(CFLAGS)
HOST_CC = gcc $(CFLAGS)

all: toybox

.PHONY: clean

include kconfig/Makefile

.config: Config.in toys/Config.in

# The long and roundabout sed is to make old versions of sed happy.  New ones
# have '\n' so can replace one line with two without all the branches and
# mucking about with hold space.
gen_config.h: .config
	sed -n -e 's/^# CONFIG_\(.*\) is not set.*/\1/' \
	  -e 't notset' -e 'b tryisset' -e ':notset' \
	  -e 'h' -e 's/.*/#define CFG_& 0/p' \
	  -e 'g' -e 's/.*/#define USE_&(...)/p' -e 'd' -e ':tryisset' \
	  -e 's/^CONFIG_\(.*\)=y.*/\1/' -e 't isset' -e 'd' -e ':isset' \
	  -e 'h' -e 's/.*/#define CFG_& 1/p' \
	  -e 'g' -e 's/.*/#define USE_&(...) __VA_ARGS__/p' $< > $@

# Actual build

toyfiles = main.c toys/*.c lib/*.c
toybox: gen_config.h $(toyfiles) lib/lib.h toys.h
	$(CC) -Wall -Os -s -funsigned-char $(CFLAGS) -I . \
		$(toyfiles) -o toybox -ffunction-sections -fdata-sections -Wl,--gc-sections

clean::
	rm -f toybox gen_config.h

distclean: clean
	rm -f .config
