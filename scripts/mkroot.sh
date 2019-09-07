#!/bin/bash

# Clear environment variables by restarting script w/bare minimum passed through
[ -z "$NOCLEAR" ] &&
  exec env -i NOCLEAR=1 HOME="$HOME" PATH="$PATH" LINUX="$LINUX" \
    CROSS_COMPILE="$CROSS_COMPILE" CROSS_SHORT="$CROSS_SHORT" "$0" "$@"

# assign command line NAME=VALUE args to env vars
while [ $# -ne 0 ]
do
  X="${1/=*/}"
  Y="${1#*=}"
  [ "${1/=/}" != "$1" ] && eval "export $X=\"\$Y\"" || echo "unknown $i"
  shift
done

# If we're cross compiling, set appropriate environment variables.
if [ -z "$CROSS_COMPILE" ]
then
  echo "Building natively"
  if ! cc --static -xc - -o /dev/null <<< "int main(void) {return 0;}"
  then
    echo "Warning: host compiler can't create static binaries." >&2
    sleep 3
  fi
else
  CROSS_PATH="$(dirname "$(which "${CROSS_COMPILE}cc")")"
  CROSS_BASE="$(basename "$CROSS_COMPILE")"
  [ -z "$CROSS_SHORT" ] && CROSS_SHORT="${CROSS_BASE/-*/}"
  echo "Cross compiling to $CROSS_SHORT"
  if [ -z "$CROSS_PATH" ]
  then
    echo "no ${CROSS_COMPILE}cc in path" >&2
    exit 1
  fi
fi

# set up directories (can override most of these paths on cmdline)
TOP="$PWD/root"
[ -z "$BUILD" ] && BUILD="$TOP/build"
[ -z "$AIRLOCK" ] && AIRLOCK="$TOP/airlock"
[ -z "$OUTPUT" ] && OUTPUT="$TOP/${CROSS_SHORT:-host}"
[ -z "$ROOT" ] && ROOT="$OUTPUT/${CROSS_BASE}fs" && rm -rf "$ROOT"
MYBUILD="$BUILD/${CROSS_BASE:-host-}tmp"
rm -rf "$MYBUILD" && mkdir -p "$MYBUILD" || exit 1

# Stabilize cross compiling by providing known $PATH contents
if [ ! -z "$CROSS_COMPILE" ]
then
  if [ ! -e "$AIRLOCK/toybox" ]
  then
    echo === Create airlock dir

    PREFIX="$AIRLOCK" KCONFIG_CONFIG="$TOP"/.airlock CROSS_COMPILE= \
      make clean defconfig toybox install_airlock &&
    rm "$TOP"/.airlock || exit 1
  fi
  export PATH="$CROSS_PATH:$AIRLOCK"
fi

### Create files and directories
mkdir -p "$ROOT"/{etc,tmp,proc,sys,dev,home,mnt,root,usr/{bin,sbin,lib},var} &&
chmod a+rwxt "$ROOT"/tmp && ln -s usr/{bin,sbin,lib} "$ROOT" || exit 1

# init script. Runs as pid 1 from initramfs to set up and hand off system.
cat > "$ROOT"/init << 'EOF' &&
#!/bin/sh

export HOME=/home
export PATH=/bin:/sbin

mountpoint -q proc || mount -t proc proc proc
mountpoint -q sys || mount -t sysfs sys sys
if ! mountpoint -q dev
then
  mount -t devtmpfs dev dev || mdev -s
  mkdir -p dev/pts
  mountpoint -q dev/pts || mount -t devpts dev/pts dev/pts
fi

if [ $$ -eq 1 ]
then
  # Setup networking for QEMU (needs /proc)
  ifconfig eth0 10.0.2.15
  route add default gw 10.0.2.2
  [ "$(date +%s)" -lt 1000 ] && rdate 10.0.2.2 # or time-b.nist.gov
  [ "$(date +%s)" -lt 10000000 ] && ntpd -nq -p north-america.pool.ntp.org

  [ -z "$CONSOLE" ] &&
    CONSOLE="$(sed -n 's@.* console=\(/dev/\)*\([^ ]*\).*@\2@p' /proc/cmdline)"

  [ -z "$HANDOFF" ] && HANDOFF=/bin/sh && echo Type exit when done.
  [ -z "$CONSOLE" ] && CONSOLE=console
  exec /sbin/oneit -c /dev/"$CONSOLE" $HANDOFF
else
  /bin/sh
  umount /dev/pts /dev /sys /proc
fi
EOF
chmod +x "$ROOT"/init &&

# /etc/passwd with both kernel special accounts (root and nobody) + guest user
cat > "$ROOT"/etc/passwd << 'EOF' &&
root::0:0:root:/home/root:/bin/sh
guest:x:500:500:guest:/home/guest:/bin/sh
nobody:x:65534:65534:nobody:/proc/self:/dev/null
EOF

# /etc/group with groups corresponding to each /etc/passwd user
cat > "$ROOT"/etc/group << 'EOF' &&
root:x:0:
guest:x:500:
nobody:x:65534:
EOF

# /etc/resolv.conf using Google's public nameserver. (We could use QEMU's
# 10.0.2.2 forwarder here, but this way works in both chroot and QEMU.)
echo "nameserver 8.8.8.8" > "$ROOT"/etc/resolv.conf || exit 1

# Build toybox

make clean
if [ -z .config ]
then
  make defconfig
  # Work around musl-libc design flaw.
  [ "${CROSS_BASE/fdpic//}" != "$CROSS_BASE" ] &&
    sed -i 's/.*\(CONFIG_TOYBOX_MUSL_NOMMU_IS_BROKEN\).*/\1=y/' .config
else
  make silentoldconfig
fi
LDFLAGS=--static PREFIX="$ROOT" make toybox install || exit 1

# Abort early if no kernel source specified
if [ -z "$LINUX" ] || [ ! -d "$LINUX/kernel" ]
then
  echo 'No $LINUX directory, kernel build skipped.'
  rmdir "$MYBUILD" "$BUILD" 2>/dev/null
  exit 0
fi

# Which architecture are we building a kernel for?
[ -z "$TARGET" ] && TARGET="${CROSS_BASE/-*/}"
[ -z "$TARGET" ] && TARGET="$(uname -m)"

# Target-specific info in an (alphabetical order) if/else staircase
# Each target needs board config, serial console, RTC, ethernet, block device.

if [ "$TARGET" == armv5l ]
then

  # This could use the same VIRT board as armv7, but let's demonstrate a
  # different one requiring a separate device tree binary.
  QEMU="qemu-system-arm -M versatilepb -net nic,model=rtl8139 -net user"
  KARCH=arm
  KARGS="console=ttyAMA0"
  VMLINUX=arch/arm/boot/zImage
  KERNEL_CONFIG="
CONFIG_CPU_ARM926T=y
CONFIG_MMU=y
CONFIG_VFP=y
CONFIG_ARM_THUMB=y
CONFIG_AEABI=y
CONFIG_ARCH_VERSATILE=y

# The switch to device-tree-only added this mess
CONFIG_ATAGS=y
CONFIG_DEPRECATED_PARAM_STRUCT=y
CONFIG_ARM_ATAG_DTB_COMPAT=y
CONFIG_ARM_ATAG_DTB_COMPAT_CMDLINE_EXTEND=y

CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y

CONFIG_RTC_CLASS=y
CONFIG_RTC_DRV_PL031=y
CONFIG_RTC_HCTOSYS=y

CONFIG_PCI=y
CONFIG_PCI_VERSATILE=y
CONFIG_BLK_DEV_SD=y
CONFIG_SCSI=y
CONFIG_SCSI_LOWLEVEL=y
CONFIG_SCSI_SYM53C8XX_2=y
CONFIG_SCSI_SYM53C8XX_DMA_ADDRESSING_MODE=0
CONFIG_SCSI_SYM53C8XX_MMIO=y

CONFIG_NET_VENDOR_REALTEK=y
CONFIG_8139CP=y
"
  DTB=arch/arm/boot/dts/versatile-pb.dtb
elif [ "$TARGET" == armv7l ] || [ "$TARGET" == aarch64 ]
then
  if [ "$TARGET" == aarch64 ]
  then
    QEMU="qemu-system-aarch64 -M virt -cpu cortex-a57"
    KARCH=arm64
    VMLINUX=arch/arm64/boot/Image
  else
    QEMU="qemu-system-arm -M virt"
    KARCH=arm
    VMLINUX=arch/arm/boot/zImage
  fi
  KARGS="console=ttyAMA0"
  KERNEL_CONFIG="
CONFIG_MMU=y
CONFIG_ARCH_MULTI_V7=y
CONFIG_ARCH_VIRT=y
CONFIG_SOC_DRA7XX=y
CONFIG_ARCH_OMAP2PLUS_TYPICAL=y
CONFIG_ARCH_ALPINE=y
CONFIG_ARM_THUMB=y
CONFIG_VDSO=y
CONFIG_CPU_IDLE=y
CONFIG_ARM_CPUIDLE=y
CONFIG_KERNEL_MODE_NEON=y

CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y

CONFIG_RTC_CLASS=y
CONFIG_RTC_HCTOSYS=y
CONFIG_RTC_DRV_PL031=y

CONFIG_NET_CORE=y
CONFIG_VIRTIO_MENU=y
CONFIG_VIRTIO_NET=y

CONFIG_PCI=y
CONFIG_PCI_HOST_GENERIC=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_MMIO=y

CONFIG_ATA=y
CONFIG_ATA_SFF=y
CONFIG_ATA_BMDMA=y
CONFIG_ATA_PIIX=y

CONFIG_PATA_PLATFORM=y
CONFIG_PATA_OF_PLATFORM=y
CONFIG_ATA_GENERIC=y
"
elif [ "$TARGET" == i486 ] || [ "$TARGET" == i686 ] ||
     [ "$TARGET" == x86_64 ] || [ "$TARGET" == x32 ]
then
  if [ "$TARGET" == i486 ]
  then
    QEMU="qemu-system-i386 -cpu 486 -global fw_cfg.dma_enabled=false"
    KERNEL_CONFIG="CONFIG_M486=y"
  elif [ "$TARGET" == i686 ]
  then
    QEMU="qemu-system-i386 -cpu pentium3"
    KERNEL_CONFIG="CONFIG_MPENTIUMII=y"
  else
    QEMU=qemu-system-x86_64
    KERNEL_CONFIG="CONFIG_64BIT=y"
    [ "$TARGET" == x32 ] && KERNEL_CONFIG="$KERNEL_CONFIG
CONFIG_X86_X32=y"
  fi
  KARCH=x86
  KARGS="console=ttyS0"
  VMLINUX=arch/x86/boot/bzImage
  CONFIG_MPENTIUMII=y
  KERNEL_CONFIG="
$KERNEL_CONFIG

CONFIG_UNWINDER_FRAME_POINTER=y

CONFIG_PCI=y
CONFIG_BLK_DEV_SD=y
CONFIG_ATA=y
CONFIG_ATA_SFF=y
CONFIG_ATA_BMDMA=y
CONFIG_ATA_PIIX=y

CONFIG_NET_VENDOR_INTEL=y
CONFIG_E1000=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_RTC_CLASS=y
"
elif [ "$TARGET" == mips ] || [ "$TARGET" == mipsel ]
then
  QEMU="qemu-system-mips -M malta"
  KARCH=mips
  KARGS="console=ttyS0"
  VMLINUX=vmlinux
  KERNEL_CONFIG="
CONFIG_MIPS_MALTA=y
CONFIG_CPU_MIPS32_R2=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y

CONFIG_PCI=y
CONFIG_BLK_DEV_SD=y
CONFIG_ATA=y
CONFIG_ATA_SFF=y
CONFIG_ATA_BMDMA=y
CONFIG_ATA_PIIX=y

CONFIG_NET_VENDOR_AMD=y
CONFIG_PCNET32=y

CONFIG_POWER_RESET=y
CONFIG_POWER_RESET_SYSCON=y
"
  [ "$TARGET" == mipsel ] &&
    KERNEL_CONFIG="${KERNEL_CONFIG}CONFIG_CPU_LITTLE_ENDIAN=y" &&
    QEMU="qemu-system-mipsel -M malta"
elif [ "$TARGET" == powerpc ]
then
  KARCH=powerpc
  QEMU="qemu-system-ppc -M g3beige"
  KARGS="console=ttyS0"
  VMLINUX=vmlinux
  KERNEL_CONFIG="
CONFIG_ALTIVEC=y
CONFIG_PPC_PMAC=y
CONFIG_PPC_OF_BOOT_TRAMPOLINE=y

CONFIG_IDE=y
CONFIG_IDE_GD=y
CONFIG_IDE_GD_ATA=y
CONFIG_BLK_DEV_IDE_PMAC=y
CONFIG_BLK_DEV_IDE_PMAC_ATA100FIRST=y

CONFIG_MACINTOSH_DRIVERS=y
CONFIG_ADB=y
CONFIG_ADB_CUDA=y

CONFIG_NET_VENDOR_NATSEMI=y
CONFIG_NET_VENDOR_8390=y
CONFIG_NE2K_PCI=y

CONFIG_SERIO=y
CONFIG_SERIAL_PMACZILOG=y
CONFIG_SERIAL_PMACZILOG_TTYS=y
CONFIG_SERIAL_PMACZILOG_CONSOLE=y
CONFIG_BOOTX_TEXT=y
"
elif [ "$TARGET" == powerpc64le ]
then
  KARCH=powerpc
  QEMU="qemu-system-ppc64 -M pseries -vga none"
  KARGS="console=/dev/hvc0"
  VMLINUX=vmlinux
  KERNEL_CONFIG="CONFIG_PPC64=y
CONFIG_PPC_PSERIES=y
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_PPC_OF_BOOT_TRAMPOLINE=y

CONFIG_BLK_DEV_SD=y
CONFIG_SCSI_LOWLEVEL=y
CONFIG_SCSI_IBMVSCSI=y
CONFIG_ATA=y

CONFIG_NET_VENDOR_IBM=y
CONFIG_IBMVETH=y
CONFIG_HVC_CONSOLE=y

# None of this should be necessary
CONFIG_PPC_TRANSACTIONAL_MEM=y
CONFIG_PPC_DISABLE_WERROR=y
CONFIG_SECTION_MISMATCH_WARN_ONLY=y
"
elif [ "$TARGET" = s390x ]
then
  QEMU="qemu-system-s390x"
  KARCH=s390
  VMLINUX=arch/s390/boot/bzImage
  KERNEL_CONFIG="
CONFIG_MARCH_Z900=y
CONFIG_PACK_STACK=y
CONFIG_NET_CORE=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_BLK=y
CONFIG_SCLP_TTY=y
CONFIG_SCLP_CONSOLE=y
CONFIG_SCLP_VT220_TTY=y
CONFIG_SCLP_VT220_CONSOLE=y
CONFIG_S390_GUEST=y
"
elif [ "$TARGET" == sh4 ]
then
  QEMU="qemu-system-sh4 -M r2d -serial null -serial mon:stdio"
  KARCH=sh
  KARGS="console=ttySC1 noiotrap"
  VMLINUX=arch/sh/boot/zImage
  KERNEL_CONFIG="
CONFIG_CPU_SUBTYPE_SH7751R=y
CONFIG_MMU=y
CONFIG_MEMORY_START=0x0c000000
CONFIG_VSYSCALL=y
CONFIG_SH_FPU=y
CONFIG_SH_RTS7751R2D=y
CONFIG_RTS7751R2D_PLUS=y
CONFIG_SERIAL_SH_SCI=y
CONFIG_SERIAL_SH_SCI_CONSOLE=y

CONFIG_PCI=y
CONFIG_NET_VENDOR_REALTEK=y
CONFIG_8139CP=y

CONFIG_PCI=y
CONFIG_BLK_DEV_SD=y
CONFIG_ATA=y
CONFIG_ATA_SFF=y
CONFIG_ATA_BMDMA=y
CONFIG_PATA_PLATFORM=y

CONFIG_BINFMT_ELF_FDPIC=y
CONFIG_BINFMT_FLAT=y

#CONFIG_SPI=y
#CONFIG_SPI_SH_SCI=y
#CONFIG_MFD_SM501=y

#CONFIG_RTC_CLASS=y
#CONFIG_RTC_DRV_R9701=y
#CONFIG_RTC_DRV_SH=y
#CONFIG_RTC_HCTOSYS=y
"
else
  echo "Unknown \$TARGET"
  exit 1
fi

# Write the miniconfig file
{
  echo "# make ARCH=$KARCH allnoconfig KCONFIG_ALLCONFIG=$TARGET.miniconf"
  echo "# make ARCH=$KARCH -j \$(nproc)"
  echo "# boot $VMLINUX"
  echo
  echo "$KERNEL_CONFIG"

  # Generic options for all targets

  echo "
# CONFIG_EMBEDDED is not set
CONFIG_EARLY_PRINTK=y
CONFIG_BINFMT_ELF=y
CONFIG_BINFMT_SCRIPT=y
CONFIG_NO_HZ=y
CONFIG_HIGH_RES_TIMERS=y

CONFIG_BLK_DEV=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y

CONFIG_BLK_DEV_LOOP=y
CONFIG_EXT4_FS=y
CONFIG_EXT4_USE_FOR_EXT2=y
CONFIG_VFAT_FS=y
CONFIG_FAT_DEFAULT_UTF8=y
CONFIG_MISC_FILESYSTEMS=y
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_XATTR=y
CONFIG_SQUASHFS_ZLIB=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_TMPFS=y
CONFIG_TMPFS_POSIX_ACL=y

CONFIG_NET=y
CONFIG_PACKET=y
CONFIG_UNIX=y
CONFIG_INET=y
CONFIG_IPV6=y
CONFIG_NETDEVICES=y
#CONFIG_NET_CORE=y
#CONFIG_NETCONSOLE=y
CONFIG_ETHERNET=y
"
} > "$OUTPUT/miniconfig-$TARGET"

# Write the qemu launch script
echo "$QEMU -nographic -no-reboot -m 256" \
     "-append \"panic=1 HOST=$TARGET $KARGS\"" \
     "-kernel $(basename "$VMLINUX") -initrd ${CROSS_BASE}root.cpio.gz" \
     ${DTB:+-dtb "$(basename "$DTB")"} '"$@"' \
     > "$OUTPUT/qemu-$TARGET.sh" &&
chmod +x "$OUTPUT/qemu-$TARGET.sh" &&

echo "Build linux for $KARCH"

# Snapshot Linux source dir and clean it
cp -sfR "$LINUX" "$MYBUILD/linux" && pushd "$MYBUILD/linux" > /dev/null ||
  exit 1

# Build kernel
make distclean &&
make ARCH=$KARCH allnoconfig KCONFIG_ALLCONFIG="$OUTPUT/miniconfig-$TARGET" &&
make ARCH=$KARCH CROSS_COMPILE="$CROSS_COMPILE" -j $(nproc) || exit 1

# If we have a device tree binary, save it for QEMU.
if [ ! -z "$DTB" ]
then
  cp "$DTB" "$OUTPUT/$(basename "$DTB")" || exit 1
fi

cp "$VMLINUX" "$OUTPUT/$(basename "$VMLINUX")" && cd .. && rm -rf linux &&
  popd || exit 1
rmdir "$MYBUILD" "$BUILD" 2>/dev/null

# package root filesystem for initramfs.
# we do it here so module install can add files (not implemented yet)
echo === create "${CROSS_BASE}root.cpio.gz"

(cd "$ROOT" && find . | cpio -o -H newc | gzip) > \
  "$OUTPUT/${CROSS_BASE}root.cpio.gz"
