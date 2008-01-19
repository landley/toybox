# ===========================================================================
# Kernel configuration targets
# These targets are used from top-level makefile

KCONFIG_TOP = Config.in
KCONFIG_PROJECT = ToyBox
obj = ./kconfig
PHONY += clean help oldconfig menuconfig config silentoldconfig \
	randconfig allyesconfig allnoconfig allmodconfig defconfig

menuconfig: $(obj)/mconf $(KCONFIG_TOP)
	$< $(KCONFIG_TOP)

config: $(obj)/conf $(KCONFIG_TOP)
	$< $(KCONFIG_TOP)

oldconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -o $(KCONFIG_TOP)

silentoldconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -s $(KCONFIG_TOP)

randconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -r $(KCONFIG_TOP)

allyesconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -y $(KCONFIG_TOP)

allnoconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -n $(KCONFIG_TOP)

defconfig: $(obj)/conf $(KCONFIG_TOP)
	$< -D /dev/null $(KCONFIG_TOP)

# Help text used by make help
help::
	@echo  '  config	  - Update current config utilising a line-oriented program'
	@echo  '  menuconfig	  - Update current config utilising a menu based program'
	@echo  '  oldconfig	  - Update current config utilising a provided .config as base'
	@echo  '  silentoldconfig - Same as oldconfig, but quietly'
	@echo  '  randconfig	  - New config with random answer to all options'
	@echo  '  defconfig	  - New config with default answer to all options'
	@echo  '  allyesconfig	  - New config where all options are accepted with yes'
	@echo  '  allnoconfig	  - New config where all options are answered with no'

# Cheesy build

SHIPPED = kconfig/zconf.tab.c kconfig/lex.zconf.c kconfig/zconf.hash.c

%.c: %.c_shipped
	@ln -s $(notdir $<) $@

gen_config.h: .config

kconfig/mconf: $(SHIPPED)
	$(HOSTCC) -o $@ kconfig/mconf.c kconfig/zconf.tab.c \
		kconfig/lxdialog/*.c -lcurses -DCURSES_LOC="<ncurses.h>" \
		-DKBUILD_NO_NLS=1 -DPROJECT_NAME=\"$(KCONFIG_PROJECT)\"

kconfig/conf: $(SHIPPED)
	$(HOSTCC) -o $@ kconfig/conf.c kconfig/zconf.tab.c -DKBUILD_NO_NLS=1 \
		-DPROJECT_NAME=\"$(KCONFIG_PROJECT)\"

clean::
	rm -f $(wildcard kconfig/*zconf*.c) kconfig/conf kconfig/mconf
