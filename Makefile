# Simple makefile wrapper, see "make help". Mostly calls various build scripts:

# scripts/make.sh - compile toybox
# scripts/install.sh - install toybox
# scripts/genconfig.sh - create/modify .config file
# scripts/test.sh - run tests against command(s)
# scripts/single.sh - build standalone command(s)
# scripts/change.sh - build all commands standalone
# mkroot/mkroot.sh - build self-contained test system bootable under qemu

export CROSS_COMPILE CFLAGS OPTIMIZE LDOPTIMIZE CC HOSTCC V STRIP ASAN

all: toybox

export KCONFIG_CONFIG ?= .config

toybox generated/unstripped/toybox: $(KCONFIG_CONFIG) *.[ch] lib/*.[ch] toys/*/*.c scripts/*.sh
	scripts/make.sh

.PHONY: clean distclean baseline bloatcheck install install_flat \
	uninstall uninstall_flat tests help change defconfig \
	list list_example list_pending root run_root \
	defconfig randconfig allyesconfig allnoconfig silentoldconfig \
	macos_defconfig bsd_defconfig android_defconfig

.SUFFIXES: # Disable legacy behavior

include kconfig/Makefile
-include .singlemake

$(KCONFIG_CONFIG): Config.in generated/Config.in
	@if [ -e "$(KCONFIG_CONFIG)" ]; then \
	KCONFIG_ALLCONFIG=$(KCONFIG_CONFIG) scripts/genconfig.sh -d; \
	else echo "Not configured (run '$(MAKE) defconfig' or '$(MAKE) menuconfig')";\
	exit 1; fi

generated/Config.in: toys/*/*.c scripts/genconfig.sh scripts/kconfig.c

defconfig:
	scripts/genconfig.sh -d

randconfig:
	scripts/genconfig.sh -r

allyesconfig:
	scripts/genconfig.sh -y

allnoconfig:
	scripts/genconfig.sh -n

silentoldconfig:
	KCONFIG_ALLCONFIG=$(KCONFIG_CONFIG) scripts/genconfig.sh -d

macos_defconfig:
	KCONFIG_ALLCONFIG=scripts/macos_miniconfig scripts/genconfig.sh -n

bsd_defconfig:
	KCONFIG_ALLCONFIG=scripts/bsd_miniconfig scripts/genconfig.sh -n

android_defconfig:
	KCONFIG_ALLCONFIG=scripts/android_miniconfig scripts/genconfig.sh -n

# Development targets
baseline: generated/unstripped/toybox
	@cp generated/unstripped/toybox generated/unstripped/toybox_old

bloatcheck: generated/unstripped/toybox_old generated/unstripped/toybox
	@scripts/probes/bloatcheck generated/unstripped/toybox_old generated/unstripped/toybox

install_flat: toybox
	scripts/install.sh --symlink --force

install_airlock: toybox
	scripts/install.sh --symlink --force --airlock

install: toybox
	scripts/install.sh --long --symlink --force

uninstall_flat:
	scripts/install.sh --uninstall

uninstall:
	scripts/install.sh --long --uninstall

change:
	scripts/change.sh

root_clean:
	@rm -rf root
	@echo root cleaned

clean::
	@chmod -fR 700 generated 2>/dev/null || true
	@rm -rf toybox generated change install .singleconfig*
	@rm -rf prereq prereq.mini toybox-prereq
	@echo cleaned

# If singlemake was in generated/ "make clean; make test_ls" wouldn't work.
distclean: clean root_clean
	@rm -f toybox* .config* .singlemake
	@echo removed .config

tests: ASAN=1
tests: toybox
	scripts/test.sh

root:
	mkroot/mkroot.sh $(MAKEFLAGS)

run_root:
	root/"$${CROSS:-host}"/run-qemu.sh

help::
	@cat scripts/help.txt
