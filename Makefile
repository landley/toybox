# Makefile for toybox.
# Copyright 2006 Rob Landley <rob@landley.net>

# If people set these on the make command line, use 'em
# Note that CC defaults to "cc" so the one in configure doesn't get
# used when scripts/make.sh and care called through "make".

HOSTCC?=cc

export CROSS_COMPILE CFLAGS OPTIMIZE LDOPTIMIZE CC HOSTCC V

all: toybox

KCONFIG_CONFIG ?= .config

toybox_stuff: $(KCONFIG_CONFIG) *.[ch] lib/*.[ch] toys/*.h toys/*/*.c scripts/*.sh

toybox generated/unstripped/toybox: toybox_stuff
	scripts/make.sh

.PHONY: clean distclean baseline bloatcheck install install_flat \
	uinstall uninstall_flat tests help toybox_stuff change \
	list list_working list_pending

include kconfig/Makefile
-include .singlemake

$(KCONFIG_CONFIG): $(KCONFIG_TOP)
$(KCONFIG_TOP): generated/Config.in
generated/Config.in: toys/*/*.c scripts/genconfig.sh
	scripts/genconfig.sh

# Development targets
baseline: generated/unstripped/toybox
	@cp generated/unstripped/toybox generated/unstripped/toybox_old

bloatcheck: generated/unstripped/toybox_old generated/unstripped/toybox
	@scripts/bloatcheck generated/unstripped/toybox_old generated/unstripped/toybox

install_flat:
	scripts/install.sh --symlink --force

install_airlock:
	scripts/install.sh --symlink --force --airlock

install:
	scripts/install.sh --long --symlink --force

uninstall_flat:
	scripts/install.sh --uninstall

uninstall:
	scripts/install.sh --long --uninstall

change:
	scripts/change.sh

clean::
	rm -rf toybox generated change .singleconfig*

# If singlemake was in generated/ "make clean; make test_ls" wouldn't work.
distclean: clean
	rm -f toybox_old .config* .singlemake

tests:
	scripts/test.sh

help::
	@echo  '  toybox          - Build toybox.'
	@echo  '  COMMANDNAME     - Build individual toybox command as a standalone binary.'
	@echo  '  list            - List COMMANDNAMEs you can build standalone.'
	@echo  '  list_pending    - List unfinished COMMANDNAMEs out of toys/pending.'
	@echo  '  change          - Build each command standalone under change/.'
	@echo  '  baseline        - Create toybox_old for use by bloatcheck.'
	@echo  '  bloatcheck      - Report size differences between old and current versions'
	@echo  '  test_COMMAND    - Run tests for COMMAND (test_ps, test_cat, etc.)'
	@echo  '  tests           - Run test suite against all compiled commands.'
	@echo  '                    export TEST_HOST=1 to test host command, VERBOSE=1'
	@echo  '                    to show diff, VERBOSE=fail to stop after first failure.'
	@echo  '  clean           - Delete temporary files.'
	@echo  "  distclean       - Delete everything that isn't shipped."
	@echo  '  install_airlock - Install toybox and host toolchain into $$PREFIX directory'
	@echo  '                    (providing $$PATH for hermetic builds).'
	@echo  '  install_flat    - Install toybox into $$PREFIX directory.'
	@echo  '  install         - Install toybox into subdirectories of $$PREFIX.'
	@echo  '  uninstall_flat  - Remove toybox from $$PREFIX directory.'
	@echo  '  uninstall       - Remove toybox from subdirectories of $$PREFIX.'
	@echo  ''
	@echo  'example: CFLAGS="--static" CROSS_COMPILE=armv5l- make defconfig toybox install'
	@echo  ''
