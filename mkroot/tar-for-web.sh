#!/bin/bash

# tar up completed system images to send to website, with READMEs

for i in root/*/run-qemu.sh
do
  i=${i%/run-qemu.sh} j=${i#root/}
  [ ! -e "$i" ] && continue
  # Add README, don't include "fs" dir (you can extract it from cpio.gz)
  cp mkroot/README.root $i/docs/README &&
  tar cvzfC $i.tgz root --exclude=fs $j || break
done

# Generate top level README
KVERS=$(toybox sed -n '3s@# Linux/[^ ]* \(.*\) Kernel Configuration@\1@p' root/*/docs/linux-fullconfig)
cat > root/README << EOF
Bootable system images created by:

  mkroot/mkroot.sh LINUX=~/linux CROSS=allnonstop

Each system image is built from two packages: toybox and linux.
Run the ./qemu-*.sh script in each tarball to boot the system
to a shell prompt under qemu. Run the "exit" command to shut down the
virtual system and exit the emulator.

See https://landley.net/toybox/FAQ.html#mkroot for details.

Built from mkroot $(git describe --tags), and Linux $KVERS with patches in linux-patches/
EOF

# scp root/*.tgz root/README website:dir
