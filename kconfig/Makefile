# ===========================================================================
# Kernel configuration targets
# These targets are used from top-level makefile

KCONFIG_TOP = Config.in
KCONFIG_PROJECT = ToyBox
obj = ./kconfig
PHONY += clean help oldconfig menuconfig config

menuconfig: $(obj)/mconf $(KCONFIG_TOP)
	scripts/genconfig.sh
	$< $(KCONFIG_TOP)

config: $(obj)/conf $(KCONFIG_TOP)
	scripts/genconfig.sh
	$< $(KCONFIG_TOP)

oldconfig: $(obj)/conf $(KCONFIG_TOP)
	scripts/genconfig.sh
	$< -o $(KCONFIG_TOP)

# Help text used by make help
help::
	@echo  '  config	  - Update current config utilising a line-oriented program'
	@echo  '  menuconfig	  - Update current config utilising a menu based program'
	@echo  '  oldconfig	  - Update current config utilising a provided .config as base'

# Cheesy build

SHIPPED = kconfig/zconf.tab.c kconfig/lex.zconf.c kconfig/zconf.hash.c

%.c: %.c_shipped
	@ln -s $(notdir $<) $@

gen_config.h: .config

kconfig/mconf: $(SHIPPED)
	cc -o $@ kconfig/mconf.c kconfig/zconf.tab.c \
		kconfig/lxdialog/*.c -lcurses -DCURSES_LOC="<ncurses.h>" \
		-DKBUILD_NO_NLS=1 -DPROJECT_NAME=\"$(KCONFIG_PROJECT)\"

kconfig/conf: $(SHIPPED)
	cc -o $@ kconfig/conf.c kconfig/zconf.tab.c -DKBUILD_NO_NLS=1 \
		-DPROJECT_NAME=\"$(KCONFIG_PROJECT)\"

clean::
	@rm -f $(wildcard kconfig/*zconf*.c) kconfig/conf kconfig/mconf
