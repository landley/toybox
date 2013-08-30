# Makefile for toybox.
# Copyright 2006 Rob Landley <rob@landley.net>

all: toybox

KCONFIG_CONFIG ?= .config
toybox toybox_unstripped: $(KCONFIG_CONFIG) *.[ch] lib/*.[ch] toys/*.h toys/*/*.c scripts/*.sh
	scripts/make.sh

.PHONY: clean distclean baseline bloatcheck install install_flat \
	uinstall uninstall_flat test tests help scripts/test

include kconfig/Makefile

$(KCONFIG_TOP): generated/Config.in
generated/Config.in: toys/*/*.c scripts/genconfig.sh
	scripts/genconfig.sh

HOSTCC?=cc

# Development targets
baseline: toybox_unstripped
	@cp toybox_unstripped toybox_old

bloatcheck: toybox_old toybox_unstripped
	@scripts/bloatcheck toybox_old toybox_unstripped

instlist: toybox
	$(HOSTCC) -I . scripts/install.c -o instlist

install_flat: instlist
	scripts/install.sh --symlink --force

install:
	scripts/install.sh --long --symlink --force

uninstall_flat: instlist
	scripts/install.sh --uninstall

uninstall:
	scripts/install.sh --long --uninstall

clean::
	rm -rf toybox toybox_unstripped generated/config.h generated/Config.in \
		generated/newtoys.h generated/globals.h instlist testdir \
		generated/Config.probed generated/oldtoys.h \
		generated/portability.h .singleconfig

distclean: clean
	rm -f toybox_old .config* generated/help.h

test: tests

tests:
	scripts/test.sh

help::
	@echo  '  toybox          - Build toybox.'
	@echo  '  baseline        - Create busybox_old for use by bloatcheck.'
	@echo  '  bloatcheck      - Report size differences between old and current versions'
	@echo  '  test            - Run test suite against compiled commands.'
	@echo  '  clean           - Delete temporary files.'
	@echo  "  distclean       - Delete everything that isn't shipped."
	@echo  '  install_flat    - Install toybox into $$PREFIX directory.'
	@echo  '  install         - Install toybox into subdirectories of $$PREFIX.'
	@echo  '  uninstall_flat  - Remove toybox from $$PREFIX directory.'
	@echo  '  uninstall       - Remove toybox from subdirectories of $$PREFIX.'
	@echo  ''
	@echo  'example: CFLAGS="--static" CROSS_COMPILE=armv5l- make defconfig toybox install'
	@echo  ''
