# Makefile for toybox.
# Copyright 2006 Rob Landley <rob@landley.net>

all: toybox

toybox toybox_unstripped: *.[ch] lib/*.[ch] toys/*.[ch] scripts/*
	scripts/make.sh

.PHONY: clean distclean baseline bloatcheck install_flat test tests help

include kconfig/Makefile

$(KCONFIG_TOP): generated/Config.in
generated/Config.in:
	scripts/genconfig.sh

HOSTCC:=cc

# Development targets
baseline: toybox_unstripped
	@cp toybox_unstripped toybox_old

bloatcheck: toybox_old toybox_unstripped
	@scripts/bloat-o-meter toybox_old toybox_unstripped

instlist: toybox
	$(HOSTCC) $(CCFLAGS) -I . scripts/install.c -o instlist

install_flat: instlist
	@mkdir -p $(PREFIX)/
	@cp toybox $(PREFIX)/
	@for i in `./instlist`; do ln -s toybox "$(PREFIX)/$$i"; done

clean::
	rm -f toybox toybox_unstripped generated/config.h generated/Config.in \
		generated/newtoys.h generated/globals.h instlist

distclean: clean
	rm -f toybox_old .config* generated/help.h

test: tests

tests:
	scripts/testall.sh

help::
	@echo  '  toybox          - Build toybox.'
	@echo  '  baseline        - Create busybox_old for use by bloatcheck.'
	@echo  '  bloatcheck      - Report size differences between old and current versions'
	@echo  '  test            - Run test suite against compiled commands.'
	@echo  '  clean           - Delete temporary files.'
	@echo  '  distclean       - Delete everything that isn't shipped.'
	@echo  '  install_flat    - Install toybox into $PREFIX directory.'
