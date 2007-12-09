# Makefile for toybox.
# Copyright 2006 Rob Landley <rob@landley.net>

CFLAGS  := $(CFLAGS) -Wall -Wundef -Wno-char-subscripts
CCFLAGS = $(CFLAGS) -funsigned-char
OPTIMIZE = -Os -ffunction-sections -fdata-sections -Wl,--gc-sections
CC      = $(CROSS_COMPILE)gcc
STRIP   = $(CROSS_COMPILE)strip
HOSTCC  = gcc

# A synonym.
CROSS_COMPILE = $(CROSS)

all: toybox

.PHONY: clean distclean baseline bloatcheck install_flat test tests help

include kconfig/Makefile

# defconfig is the "maximum sane config"; allyesconfig minus debugging and such.
#defconfig: allyesconfig
#	@sed -i -r -e "s/^(CONFIG_TOYBOX_(DEBUG|FREE))=.*/# \1 is not set/" .config

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

# Development targets
baseline: toybox_unstripped
	@cp toybox_unstripped toybox_old

bloatcheck: toybox_old toybox_unstripped
	@scripts/bloat-o-meter toybox_old toybox_unstripped

# Get list of .c files to compile, including toys/*.c files from .config
toyfiles = main.c lib/*.c \
	$(shell scripts/cfg2files.sh < .config | sed 's@\(.*\)@toys/\1.c@')

# The following still depends on toys/help.h even when it's not there, so *.h
# isn't sufficient by itself.

toybox_unstripped: gen_config.h $(toyfiles) toys/toylist.h toys/help.h toys/*.h lib/*.h toys.h
	$(CC) $(CCFLAGS) -I . $(toyfiles) -o toybox_unstripped $(OPTIMIZE)

toybox: toybox_unstripped
	$(STRIP) toybox_unstripped -o toybox

toys/help.c: toys/help.h

toys/help.h: Config.in toys/Config.in scripts/config2help.py
	scripts/config2help.py Config.in > toys/help.h

instlist: toybox
	$(HOSTCC) $(CCFLAGS) -I . scripts/install.c -o instlist

install_flat: instlist
	@mkdir -p $(PREFIX)/
	@cp toybox $(PREFIX)/
	@for i in `./instlist`; do ln -s toybox "$(PREFIX)/$$i"; done

clean::
	rm -f toybox toybox_unstripped gen_config.h instlist

distclean: clean
	rm -f toybox_old .config* toys/help.h

test: tests

tests:
	scripts/testall.sh

help::
	@echo  '  baseline        - Create busybox_old for use by bloatcheck.'
	@echo  '  bloatcheck      - Report size differences between old and current versions'
