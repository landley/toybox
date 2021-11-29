#!/bin/bash

# ----- Setup environment

die() { echo "$@" >&2; exit 1; }
announce() { echo -e "\033]2;$CROSS $*\007\n=== $*"; }

# Clear environment variables by restarting script w/bare minimum passed through
[ -z "$NOCLEAR" ] && exec env -i NOCLEAR=1 HOME="$HOME" PATH="$PATH" \
    LINUX="$LINUX" CROSS="$CROSS" CROSS_COMPILE="$CROSS_COMPILE" "$0" "$@"

# assign command line NAME=VALUE args to env vars, the rest are packages
while [ $# -ne 0 ]; do
  [ "${1/=/}" != "$1" ] && export "$1" || { [ "$1" != -- ] && PKG="PKG $1"; }
  shift
done

# Create work/output directories (command line can override these)
mkdir -p ${TOP:=$PWD/root} ${BUILD:=$TOP/build} ${LOG:=$BUILD/log} || exit 1

# ----- Set up for cross compling (optional)

if [ -n "$CROSS_COMPILE" ]; then
  CROSS_COMPILE="$(realpath -s "$CROSS_COMPILE")" # absolute path for airlock
  [ -z "$CROSS" ] && CROSS=${CROSS_COMPILE/*\//} CROSS=${CROSS/-*/}
elif [ -n "$CROSS" ]; then # CROSS=all, CROSS=target, or list available targets
  [ ! -d "${CCC:=$PWD/ccc}" ] && die "No ccc symlink to compiler directory."
  TARGETS="$(ls "$CCC" | sed -n 's/-.*//p' | sort -u)"
  if [ "${CROSS::3}" == all ]; then # loop calling ourselves for each target
    for i in $TARGETS; do
      "$0" "$@" CROSS=$i || [ "$CROSS" == allnonstop ] || exit 1
    done; exit
  else # Set $CROSS_COMPILE from $CROSS using ccc directory
    CROSS_COMPILE="$(echo "$CCC/$CROSS"-*cross/bin/"$CROSS"*-cc)"
    [ ! -e "$CROSS_COMPILE" ] && echo $TARGETS && exit # list available targets
    CROSS_COMPILE="${CROSS_COMPILE%cc}" # keep prefix, not path to compiler
  fi
fi

# Start logging
rm -f "$LOG/${CROSS:=host}".{n,y}
[ -z "$NOLOG" ] && exec > >(tee "$LOG/$CROSS.n") 2>&1
echo "Building for ${CROSS:=host}"

# ----- Build airlock (Optional)
${CROSS_COMPILE}cc --static -xc - -o /dev/null <<< "int main(void){return 0;}"||
  die "${CROSS_COMPILE}cc can't create static binaries"

if [ -n "$CROSS_COMPILE" ] && [ -z "$NOAIRLOCK" ]; then
  # When cross compiling build under a host toybox with known behavior
  if [ ! -e "${AIRLOCK:=$BUILD/airlock}/toybox" ]; then
    announce "airlock"
    PREFIX="$AIRLOCK" KCONFIG_CONFIG="$BUILD"/.airlock CROSS_COMPILE= \
      make clean defconfig toybox install_airlock &&
      rm "$BUILD"/.airlock || exit 1
  fi
  export PATH="$AIRLOCK"
fi

# Install command line recording wrapper, logs all commands run from $PATH
if [ -z "$NOLOGPATH" ]; then
  # Move cross compiler into $PATH so calls to it get logged
  [ -n "$CROSS_COMPILE" ] && PATH="${CROSS_COMPILE%/*}:$PATH" &&
    CROSS_COMPILE=${CROSS_COMPILE##*/}
  export WRAPDIR="$BUILD/record-commands" LOGPATH="$LOG/$CROSS-commands.txt"
  rm -rf "$WRAPDIR" "$LOGPATH" generated/obj &&
  WRAPDIR="$WRAPDIR" CROSS_COMPILE= source scripts/record-commands || exit 1
fi

# Create per-target work directories
MYBUILD="$BUILD/${CROSS}-tmp" && rm -rf "$MYBUILD" && mkdir -p "$MYBUILD" &&
mkdir -p ${OUTPUT:=$TOP/$CROSS} || exit 1
[ -z "$ROOT" ] && ROOT="$OUTPUT/fs" && rm -rf "$ROOT"

# ----- Create new root filesystem's directory layout.

# FHS wants boot media opt srv usr/{local,share}, stuff under /var...
mkdir -p "$ROOT"/{dev,etc/rc,home,mnt,proc,root,sys,tmp/run,usr/{bin,sbin,lib},var} &&
chmod a+rwxt "$ROOT"/tmp && ln -s usr/{bin,sbin,lib} tmp/run "$ROOT" || exit 1

# Write init script. Runs as pid 1 from initramfs to set up and hand off system.
cat > "$ROOT"/init << 'EOF' &&
#!/bin/sh

export HOME=/home PATH=/bin:/sbin

if ! mountpoint -q dev; then
  mount -t devtmpfs dev dev
  [ $$ -eq 1 ] && exec 0<>/dev/console 1>&0 2>&1
  for i in ,fd /0,stdin /1,stdout /2,stderr
  do ln -sf /proc/self/fd${i/,*/} dev/${i/*,/}; done
  mkdir -p dev/{shm,pts}
  chmod +t /dev/shm
fi
mountpoint -q dev/pts || mount -t devpts dev/pts dev/pts
mountpoint -q proc || mount -t proc proc proc
mountpoint -q sys || mount -t sysfs sys sys

if [ $$ -eq 1 ]; then # Setup networking for QEMU (needs /proc)
  ifconfig lo 127.0.0.1
  ifconfig eth0 10.0.2.15
  route add default gw 10.0.2.2
  [ "$(date +%s)" -lt 1000 ] && timeout 2 sntp -sq 10.0.2.2 # Ask host
  [ "$(date +%s)" -lt 10000000 ] && sntp -sq time.google.com

  # Run package scripts (if any)
  for i in $(ls -1 /etc/rc 2>/dev/null | sort); do . "$i"; done

  [ -z "$CONSOLE" ] && CONSOLE="$(</sys/class/tty/console/active)"
  [ -z "$HANDOFF" ] && HANDOFF=/bin/sh && echo Type exit when done.
  echo 3 > /proc/sys/kernel/printk
  exec oneit -c /dev/"${CONSOLE:-console}" $HANDOFF
else # for chroot
  /bin/sh
  umount /dev/pts /dev /sys /proc
fi
EOF
chmod +x "$ROOT"/init &&

# Google's nameserver, passwd+group with special (root/nobody) accounts + guest
echo "nameserver 8.8.8.8" > "$ROOT"/etc/resolv.conf &&
cat > "$ROOT"/etc/passwd << 'EOF' &&
root:x:0:0:root:/root:/bin/sh
guest:x:500:500:guest:/home/guest:/bin/sh
nobody:x:65534:65534:nobody:/proc/self:/dev/null
EOF
echo -e 'root:x:0:\nguest:x:500:\nnobody:x:65534:' > "$ROOT"/etc/group || exit 1

# Build static toybox with existing .config if there is one, else defconfig+sh
announce toybox
[ -e .config ] && [ -z "$PENDING" ] && CONF=silentoldconfig || unset CONF
for i in $PENDING sh route; do XX="$XX"$'\n'CONFIG_${i^^?}=y; done
LDFLAGS=--static PREFIX="$ROOT" make clean \
  ${CONF:-defconfig KCONFIG_ALLCONFIG=<(echo "$XX")} toybox install || exit 1

# Build any packages listed on command line
for i in $PKG; do
  announce "$i"
  ( PATH="${PKGDIR:=$PWD/scripts/root}:$PATH"; source plumbing; source $i ) ||
    die $i
done

# ----- Build kernel for target

if [ -z "$LINUX" ] || [ ! -d "$LINUX/kernel" ]; then
  echo 'No $LINUX directory, kernel build skipped.'
else
  # Which architecture are we building a kernel for?
  LINUX="$(realpath "$LINUX")"
  [ -z "$TARGET" ] &&
    { [ "$CROSS" == host ] && TARGET="$(uname -m)" || TARGET="$CROSS"; }

  # Target-specific info in an (alphabetical order) if/else staircase
  # Each target needs board config, serial console, RTC, ethernet, block device.

  if [ "$TARGET" == armv5l ]; then
    # This could use the same VIRT board as armv7, but let's demonstrate a
    # different one requiring a separate device tree binary.
    QEMU="arm -M versatilepb -net nic,model=rtl8139 -net user"
    KARCH=arm KARGS=ttyAMA0 VMLINUX=arch/arm/boot/zImage
    KCONF=CPU_ARM926T,MMU,VFP,ARM_THUMB,AEABI,ARCH_VERSATILE,ATAGS,DEPRECATED_PARAM_STRUCT,ARM_ATAG_DTB_COMPAT,ARM_ATAG_DTB_COMPAT_CMDLINE_EXTEND,SERIAL_AMBA_PL011,SERIAL_AMBA_PL011_CONSOLE,RTC_CLASS,RTC_DRV_PL031,RTC_HCTOSYS,PCI,PCI_VERSATILE,BLK_DEV_SD,SCSI,SCSI_LOWLEVEL,SCSI_SYM53C8XX_2,SCSI_SYM53C8XX_MMIO,NET_VENDOR_REALTEK,8139CP
    KERNEL_CONFIG="CONFIG_SCSI_SYM53C8XX_DMA_ADDRESSING_MODE=0"
    DTB=arch/arm/boot/dts/versatile-pb.dtb
  elif [ "$TARGET" == armv7l ] || [ "$TARGET" == aarch64 ]; then
    if [ "$TARGET" == aarch64 ]; then
      QEMU="aarch64 -M virt -cpu cortex-a57"
      KARCH=arm64 VMLINUX=arch/arm64/boot/Image
    else
      QEMU="arm -M virt" KARCH=arm VMLINUX=arch/arm/boot/zImage
    fi
    KARGS=ttyAMA0
    KCONF=MMU,ARCH_MULTI_V7,ARCH_VIRT,SOC_DRA7XX,ARCH_OMAP2PLUS_TYPICAL,ARCH_ALPINE,ARM_THUMB,VDSO,CPU_IDLE,ARM_CPUIDLE,KERNEL_MODE_NEON,SERIAL_AMBA_PL011,SERIAL_AMBA_PL011_CONSOLE,RTC_CLASS,RTC_HCTOSYS,RTC_DRV_PL031,NET_CORE,VIRTIO_MENU,VIRTIO_NET,PCI,PCI_HOST_GENERIC,VIRTIO_BLK,VIRTIO_PCI,VIRTIO_MMIO,ATA,ATA_SFF,ATA_BMDMA,ATA_PIIX,PATA_PLATFORM,PATA_OF_PLATFORM,ATA_GENERIC
  elif [ "$TARGET" == hexagon ]; then
    QEMU="hexagon -M comet" KARGS=ttyS0 VMLINUX=vmlinux
    KARCH="hexagon LLVM_IAS=1" KCONF=SPI,SPI_BITBANG,IOMMU_SUPPORT
  elif [ "$TARGET" == i486 ] || [ "$TARGET" == i686 ] ||
       [ "$TARGET" == x86_64 ] || [ "$TARGET" == x32 ]; then
    if [ "$TARGET" == i486 ]; then
      QEMU="i386 -cpu 486 -global fw_cfg.dma_enabled=false" KCONF=M486
    elif [ "$TARGET" == i686 ]; then
      QEMU="i386 -cpu pentium3" KCONF=MPENTIUMII
    else
      QEMU=x86_64 KCONF=64BIT
      [ "$TARGET" == x32 ] && KCONF=X86_X32
    fi
    KARCH=x86 KARGS=ttyS0 VMLINUX=arch/x86/boot/bzImage
    KCONF=$KCONF,UNWINDER_FRAME_POINTER,PCI,BLK_DEV_SD,ATA,ATA_SFF,ATA_BMDMA,ATA_PIIX,NET_VENDOR_INTEL,E1000,SERIAL_8250,SERIAL_8250_CONSOLE,RTC_CLASS
  elif [ "$TARGET" == m68k ]; then
    QEMU="m68k -M q800" KARCH=m68k KARGS=ttyS0 VMLINUX=vmlinux
    KCONF=MMU,M68040,M68KFPU_EMU,MAC,SCSI_MAC_ESP,MACINTOSH_DRIVERS,ADB,ADB_MACII,NET_CORE,MACSONIC,SERIAL_PMACZILOG,SERIAL_PMACZILOG_TTYS,SERIAL_PMACZILOG_CONSOLE
  elif [ "$TARGET" == mips ] || [ "$TARGET" == mipsel ]; then
    QEMU="mips -M malta" KARCH=mips KARGS=ttyS0 VMLINUX=vmlinux
    KCONF=MIPS_MALTA,CPU_MIPS32_R2,SERIAL_8250,SERIAL_8250_CONSOLE,PCI,BLK_DEV_SD,ATA,ATA_SFF,ATA_BMDMA,ATA_PIIX,NET_VENDOR_AMD,PCNET32,POWER_RESET,POWER_RESET_SYSCON
    [ "$TARGET" == mipsel ] && KCONF=$KCONF,CPU_LITTLE_ENDIAN &&
      QEMU="mipsel -M malta"
  elif [ "$TARGET" == powerpc ]; then
    KARCH=powerpc QEMU="ppc -M g3beige" KARGS=ttyS0 VMLINUX=vmlinux
    KCONF=ALTIVEC,PPC_PMAC,PPC_OF_BOOT_TRAMPOLINE,IDE,IDE_GD,IDE_GD_ATA,BLK_DEV_IDE_PMAC,BLK_DEV_IDE_PMAC_ATA100FIRST,MACINTOSH_DRIVERS,ADB,ADB_CUDA,NET_VENDOR_NATSEMI,NET_VENDOR_8390,NE2K_PCI,SERIO,SERIAL_PMACZILOG,SERIAL_PMACZILOG_TTYS,SERIAL_PMACZILOG_CONSOLE,BOOTX_TEXT
  elif [ "$TARGET" == powerpc64le ]; then
    KARCH=powerpc QEMU="ppc64 -M pseries -vga none" KARGS=hvc0
    VMLINUX=vmlinux
    KCONF=PPC64,PPC_PSERIES,CPU_LITTLE_ENDIAN,PPC_OF_BOOT_TRAMPOLINE,BLK_DEV_SD,SCSI_LOWLEVEL,SCSI_IBMVSCSI,ATA,NET_VENDOR_IBM,IBMVETH,HVC_CONSOLE,PPC_TRANSACTIONAL_MEM,PPC_DISABLE_WERROR,SECTION_MISMATCH_WARN_ONLY
  elif [ "$TARGET" = s390x ]; then
    QEMU="s390x" KARCH=s390 VMLINUX=arch/s390/boot/bzImage
    KCONF=MARCH_Z900,PACK_STACK,NET_CORE,VIRTIO_NET,VIRTIO_BLK,SCLP_TTY,SCLP_CONSOLE,SCLP_VT220_TTY,SCLP_VT220_CONSOLE,S390_GUEST
  elif [ "$TARGET" == sh2eb ]; then
    KARCH=sh VMLINUX=vmlinux KERNEL_CONFIG='CONFIG_MEMORY_START=0x10000000
CONFIG_CMDLINE="console=ttyUL0 earlycon"' BUILTIN=1
    KCONF=CPU_SUBTYPE_J2,CPU_BIG_ENDIAN,SH_JCORE_SOC,SMP,BINFMT_ELF_FDPIC,JCORE_EMAC,SERIAL_UARTLITE,SERIAL_UARTLITE_CONSOLE,HZ_100,CMDLINE_OVERWRITE,SPI,SPI_JCORE,MMC,PWRSEQ_SIMPLE,MMC_BLOCK,MMC_SPI
  elif [ "$TARGET" == sh4 ]; then
    QEMU="sh4 -M r2d -serial null -serial mon:stdio" KARCH=sh
    KARGS="ttySC1 noiotrap" VMLINUX=arch/sh/boot/zImage
    KERNEL_CONFIG="CONFIG_MEMORY_START=0x0c000000"
    KCONF=CPU_SUBTYPE_SH7751R,MMU,VSYSCALL,SH_FPU,SH_RTS7751R2D,RTS7751R2D_PLUS,SERIAL_SH_SCI,SERIAL_SH_SCI_CONSOLE,PCI,NET_VENDOR_REALTEK,8139CP,PCI,BLK_DEV_SD,ATA,ATA_SFF,ATA_BMDMA,PATA_PLATFORM,BINFMT_ELF_FDPIC,BINFMT_FLAT
#see also SPI SPI_SH_SCI MFD_SM501 RTC_CLASS RTC_DRV_R9701 RTC_DRV_SH RTC_HCTOSYS
  else die "Unknown \$TARGET $TARGET"
  fi

  # Write the qemu launch script
  if [ -n "$QEMU" ]; then
    [ -z "$BUILTIN" ] && INITRD="-initrd ${CROSS}root.cpio.gz"
    echo qemu-system-"$QEMU" '"$@"' $QEMU_MORE -nographic -no-reboot -m 256 \
         -kernel $(basename $VMLINUX) $INITRD \
         "-append \"panic=1 HOST=$TARGET console=$KARGS \$KARGS\"" \
         ${DTB:+-dtb "$(basename "$DTB")"} ";echo -e '\e[?7h'" \
         > "$OUTPUT/qemu-$TARGET.sh" &&
    chmod +x "$OUTPUT/qemu-$TARGET.sh" || exit 1
  fi

  announce "linux-$KARCH"
  pushd "$LINUX" && make distclean && popd &&
  cp -sfR "$LINUX" "$MYBUILD/linux" && pushd "$MYBUILD/linux" &&
  sed -is '/select HAVE_STACK_VALIDATION/d' arch/x86/Kconfig && # Fix x86-64
  sed -is 's/depends on !SMP/& || !MMU/' mm/Kconfig &&          # Fix sh2eb

  # Write miniconfig
  { echo "# make ARCH=$KARCH allnoconfig KCONFIG_ALLCONFIG=$TARGET.miniconf"
    echo -e "# make ARCH=$KARCH -j \$(nproc)\n# boot $VMLINUX\n\n"
    echo "# CONFIG_EMBEDDED is not set"

    # Expand list of =y symbols, first generic then architecture-specific
    for i in BINFMT_ELF,BINFMT_SCRIPT,NO_HZ,HIGH_RES_TIMERS,BLK_DEV,BLK_DEV_INITRD,RD_GZIP,BLK_DEV_LOOP,EXT4_FS,EXT4_USE_FOR_EXT2,VFAT_FS,FAT_DEFAULT_UTF8,MISC_FILESYSTEMS,SQUASHFS,SQUASHFS_XATTR,SQUASHFS_ZLIB,DEVTMPFS,DEVTMPFS_MOUNT,TMPFS,TMPFS_POSIX_ACL,NET,PACKET,UNIX,INET,IPV6,NETDEVICES,NET_CORE,NETCONSOLE,ETHERNET,COMPAT_32BIT_TIME,EARLY_PRINTK,IKCONFIG,IKCONFIG_PROC $KCONF $KEXTRA ; do
      echo "# architecture ${X:-independent}"
      sed -E '/^$/d;s/([^,]*)($|,)/CONFIG_\1=y\n/g' <<< "$i"
      X=specific
    done
    [ -n "$BUILTIN" ] && echo -e CONFIG_INITRAMFS_SOURCE="\"$OUTPUT/fs\""
    echo "$KERNEL_CONFIG"
  } > "$OUTPUT/miniconfig-$TARGET" &&
  make ARCH=$KARCH allnoconfig KCONFIG_ALLCONFIG="$OUTPUT/miniconfig-$TARGET" &&

  # Second config pass to remove stupid kernel defaults
  # See http://lkml.iu.edu/hypermail/linux/kernel/1912.3/03493.html
  sed -e 's/# CONFIG_EXPERT .*/CONFIG_EXPERT=y/' -e "$(sed -E -e '/^$/d' \
    -e 's@([^,]*)($|,)@/^CONFIG_\1=y/d;$a# CONFIG_\1 is not set/\n@g' \
       <<< VT,SCHED_DEBUG,DEBUG_MISC,X86_DEBUG_FPU)" -i .config &&
  yes "" | make ARCH=$KARCH oldconfig > /dev/null &&

  # Build kernel. Copy config, device tree binary, and kernel binary to output
  make ARCH=$KARCH CROSS_COMPILE="$CROSS_COMPILE" -j $(nproc) &&
  cp .config "$OUTPUT/linux-fullconfig" || exit 1
  [ -n "$DTB" ] && { cp "$DTB" "$OUTPUT" || exit 1 ;}
  cp "$VMLINUX" "$OUTPUT" && cd .. && rm -rf linux && popd || exit 1
fi

# clean up and package root filesystem for initramfs.
[ -z "$BUILTIN" ] && announce "${CROSS}root.cpio.gz" &&
  (cd "$ROOT" && find . | cpio -o -H newc ${CROSS_COMPILE:+--no-preserve-owner} | gzip) \
    > "$OUTPUT/$CROSS"root.cpio.gz || exit 1

mv "$LOG/$CROSS".{n,y} #2>/dev/null
rmdir "$MYBUILD" "$BUILD" 2>/dev/null || exit 0 # remove if empty, not an error
