#!/bin/bash

# Clear environment variables by restarting script w/bare minimum passed through
[ -z "$NOCLEAR" ] && exec env -i NOCLEAR=1 HOME="$HOME" PATH="$PATH" \
    "LINUX=$LINUX" "CROSS=$CROSS" CROSS_COMPILE="$CROSS_COMPILE" "$0" "$@"

# assign command line NAME=VALUE args to env vars, keeping rest as packages
while [ $# -ne 0 ]; do
  [ "${1/=/}" != "$1" ] && eval "export ${1/=*/}=\"\${1#*=}\"" ||
    { [ "$1" != '--' ] && PKG="${PKG:-plumbing} $1"; }
  shift
done

die() { echo "$@" >&2; exit 1; }
announce() { echo -e "\033]2;$CROSS $*\007\n=== $*"; }

# Create target-independent working directories (cmdline can change locations)
TOP="$PWD/root"
mkdir -p ${BUILD:=$TOP/build} ${AIRLOCK:=$TOP/airlock} ${LOG:=$TOP/log} ||exit 1

# set CROSS_COMPILE from $CROSS using ccc. Handle "all" w/log, list, and err chk
if [ ! -z "$CROSS" ]; then
  [ ! -d "${CCC:=$PWD/ccc}" ] && die "No ccc symlink to compiler directory."
  CROSS_COMPILE="$(echo "$CCC/$CROSS"-*cross/bin/"$CROSS"*-cc | sed 's/cc$//')"
  if [ "${CROSS::3}" == all ]; then
    for i in $(ls "$CCC" | sed -n 's/-.*//p' | sort -u | xargs); do
      { rm -f "$LOG/$i-log".{failed,success}
        "$0" "$@" CROSS=$i ; [ $? -eq 0 ] && mv "$LOG/$i".{txt,success}
      } |& tee "$LOG/$i.txt"
      [ ! -e "$LOG/$i.success" ] &&
        { mv "$LOG/$i".{txt,failed};[ "$CROSS" != allnonstop ] && exit 1; }
    done
    exit
  elif [ ! -e "${CROSS_COMPILE}cc" ]; then
    ls "$CCC" | sed -n 's/-.*//p' | sort -u | xargs
    exit
  fi
fi

${CROSS_COMPILE}cc --static -xc - -o /dev/null <<< "int main(void){return 0;}"||
  die "${CROSS_COMPILE}cc can't create static binaries"

# Parse and sanity check $CROSS_COMPILE (if any)
if [ ! -z "$CROSS_COMPILE" ]; then
  CROSS_PATH="$(dirname "$(which "${CROSS_COMPILE}cc")")"
  [ -z "$CROSS_PATH" ] && die "no ${CROSS_COMPILE}cc in path"
  : ${CROSS_BASE:=$(basename "$CROSS_COMPILE")} ${CROSS:=${CROSS_BASE/-*/}}
fi
echo "Building for ${CROSS:=host}"

# Create target-specific work/output directories
: ${OUTPUT:=$TOP/$CROSS} ${PKGDIR:=$PWD/scripts/root}
MYBUILD="$BUILD/${CROSS_BASE:-host-}tmp"
rm -rf "$MYBUILD" && mkdir -p "$MYBUILD" || exit 1
[ -z "$ROOT" ] && ROOT="$OUTPUT/fs" && rm -rf "$ROOT" # only blank if NOT set

# When cross compiling build everything under a host toybox with known behavior
if [ ! -z "$CROSS_COMPILE" ]; then
  if [ ! -e "$AIRLOCK/toybox" ]; then
    announce "airlock"
    PREFIX="$AIRLOCK" KCONFIG_CONFIG="$TOP"/.airlock CROSS_COMPILE= \
      make clean defconfig toybox install_airlock && rm "$TOP"/.airlock ||exit 1
  fi
  export PATH="$CROSS_PATH:$AIRLOCK"
fi

# Create new root filesystem's directory layout
mkdir -p "$ROOT"/{etc,tmp,proc,sys,dev,home,mnt,root,usr/{bin,sbin,lib},var} &&
chmod a+rwxt "$ROOT"/tmp && ln -s usr/{bin,sbin,lib} "$ROOT" || exit 1

# Write init script. Runs as pid 1 from initramfs to set up and hand off system.
cat > "$ROOT"/init << 'EOF' &&
#!/bin/sh

export HOME=/home PATH=/bin:/sbin

if ! mountpoint -q dev; then
  mount -t devtmpfs dev dev || mdev -s
  [ $$ -eq 1 ] && exec >/dev/console 2>&1
  for i in ,fd /0,stdin /1,stdout /2,stderr
  do ln -sf /proc/self/fd${i/,*/} dev/${i/*,/}; done
  mkdir -p dev/{shm,pts}
  mountpoint -q dev/pts || mount -t devpts dev/pts dev/pts
  chmod +t /dev/shm
fi
mountpoint -q proc || mount -t proc proc proc
mountpoint -q sys || mount -t sysfs sys sys

if [ $$ -eq 1 ]; then # Setup networking for QEMU (needs /proc)
  ifconfig lo 127.0.0.1
  ifconfig eth0 10.0.2.15
  route add default gw 10.0.2.2
  [ "$(date +%s)" -lt 1000 ] && timeout 2 sntp -sq 10.0.2.2 # Ask host
  [ "$(date +%s)" -lt 10000000 ] && sntp -sq time.google.com

  # Run package scripts (if any)
  [ -e /etc/rc ] && for i in $(echo /etc/rc/* | sort); do . $i; done

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
make clean ${CONF:-defconfig KCONFIG_ALLCONFIG=<(echo "$XX")} &&
LDFLAGS=--static PREFIX="$ROOT" make toybox install || exit 1

# Build any packages listed on command line
for i in $PKG; do
  announce "$i"; PATH="$PKGDIR:$PATH" source $i; [ $? -ne 0 ] && die $i
done

if [ -z "$LINUX" ] || [ ! -d "$LINUX/kernel" ]; then
  echo 'No $LINUX directory, kernel build skipped.'
else
  # Which architecture are we building a kernel for?
  LINUX="$(realpath "$LINUX")"
  [ -z "$TARGET" ] && TARGET="${CROSS_BASE/-*/}"
  [ -z "$TARGET" ] && TARGET="$(uname -m)"

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
    KARCH=powerpc QEMU="ppc64 -M pseries -vga none" KARGS=/dev/hvc0
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
  if [ ! -z "$QEMU" ]; then
    [ -z "$BUILTIN" ] && INITRD="-initrd ${CROSS_BASE}root.cpio.gz"
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

  # Write miniconfig
  { echo "# make ARCH=$KARCH allnoconfig KCONFIG_ALLCONFIG=$TARGET.miniconf"
    echo -e "# make ARCH=$KARCH -j \$(nproc)\n# boot $VMLINUX\n\n"
    echo "# CONFIG_EMBEDDED is not set"

    # Expand list of =y symbols, first generic then architecture-specific
    for i in BINFMT_ELF,BINFMT_SCRIPT,NO_HZ,HIGH_RES_TIMERS,BLK_DEV,BLK_DEV_INITRD,RD_GZIP,BLK_DEV_LOOP,EXT4_FS,EXT4_USE_FOR_EXT2,VFAT_FS,FAT_DEFAULT_UTF8,MISC_FILESYSTEMS,SQUASHFS,SQUASHFS_XATTR,SQUASHFS_ZLIB,DEVTMPFS,DEVTMPFS_MOUNT,TMPFS,TMPFS_POSIX_ACL,NET,PACKET,UNIX,INET,IPV6,NETDEVICES,NET_CORE,NETCONSOLE,ETHERNET,COMPAT_32BIT_TIME,EARLY_PRINTK,IKCONFIG,IKCONFIG_PROC $KCONF ; do
      echo "# architecture ${X:-independent}"
      sed -E '/^$/d;s/([^,]*)($|,)/CONFIG_\1=y\n/g' <<< "$i"
      X=specific
    done
    [ ! -z "$BUILTIN" ] && echo -e CONFIG_INITRAMFS_SOURCE="\"$OUTPUT/fs\""
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
  [ ! -z "$DTB" ] && { cp "$DTB" "$OUTPUT" || exit 1 ;}
  cp "$VMLINUX" "$OUTPUT" && cd .. && rm -rf linux && popd || exit 1
fi

# clean up and package root filesystem for initramfs.
[ -z "$BUILTIN" ] && announce "${CROSS_BASE}root.cpio.gz" &&
  (cd "$ROOT" && find . | cpio -o -H newc ${CROSS_COMPILE:+--no-preserve-owner} | gzip) \
    > "$OUTPUT/$CROSS_BASE"root.cpio.gz
rmdir "$MYBUILD" "$BUILD" 2>/dev/null # remove if empty
