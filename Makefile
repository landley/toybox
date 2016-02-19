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

toybox toybox_unstripped: toybox_stuff
	scripts/make.sh

.PHONY: clean distclean baseline bloatcheck install install_flat \
	uinstall uninstall_flat test tests help toybox_stuff change \
	list list_working list_pending

include kconfig/Makefile
-include .singlemake

$(KCONFIG_CONFIG): $(KCONFIG_TOP)
$(KCONFIG_TOP): generated/Config.in
generated/Config.in: toys/*/*.c scripts/genconfig.sh
	scripts/genconfig.sh

# Development targets
baseline: toybox_unstripped
	@cp toybox_unstripped toybox_old

bloatcheck: toybox_old toybox_unstripped
	@scripts/bloatcheck toybox_old toybox_unstripped

install_flat:
	scripts/install.sh --symlink --force

install:
	scripts/install.sh --long --symlink --force

uninstall_flat:
	scripts/install.sh --uninstall

uninstall:
	scripts/install.sh --long --uninstall

change:
	scripts/change.sh

clean::
	rm -rf toybox toybox_unstripped generated change .singleconfig*

distclean: clean
	rm -f toybox_old .config* .singlemake

test: tests

tests:
	scripts/test.sh

help::
	@echo  '  toybox          - Build toybox.'
	@echo  '  COMMANDNAME     - Build individual toybox command as a standalone binary.'
	@echo  '  list            - List COMMANDNAMEs (also list_working and list_pending).'
	@echo  '  change          - Build each command standalone under change/.'
	@echo  '  baseline        - Create toybox_old for use by bloatcheck.'
	@echo  '  bloatcheck      - Report size differences between old and current versions'
	@echo  '  test_COMMAND    - Run tests for COMMAND (test_ps, test_cat, etc.)
	@echo  '  test            - Run test suite against all compiled commands.'
	@echo  '                    export TEST_HOST=1 to test host command, VERBOSE=1'
	@echo  '                    to show diff, VERBOSE=fail to stop after first failure.'
	@echo  '  clean           - Delete temporary files.'
	@echo  "  distclean       - Delete everything that isn't shipped."
	@echo  '  install_flat    - Install toybox into $$PREFIX directory.'
	@echo  '  install         - Install toybox into subdirectories of $$PREFIX.'
	@echo  '  uninstall_flat  - Remove toybox from $$PREFIX directory.'
	@echo  '  uninstall       - Remove toybox from subdirectories of $$PREFIX.'
	@echo  ''
	@echo  'example: CFLAGS="--static" CROSS_COMPILE=armv5l- make defconfig toybox install'
	@echo  ''
