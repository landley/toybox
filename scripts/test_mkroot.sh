#/bin/bash

[ -n "$(which toybox)" -a -n "$(which mksquashfs)" ] ||
  { echo "Need toybox and mksquashfs in $PATH"; exit 1; }

mkdir -p "${TEST:=root/build/test}" &&

# Setup test filesystem
cat > "$TEST"/init << EOF &&
#!/bin/sh

echo
echo === running init
[ "$(date +%s)" -gt 1500000000 ] && echo === date ok
wget http://10.0.2.2:65432 -O -
EOF
chmod +x "$TEST"/init &&
mksquashfs "$TEST"/init "$TEST"/init.sqf -noappend >/dev/null &&

# Setup for network smoke test
echo === net ok > "$TEST"/index.html || exit 1
toybox netcat -p 65432 -s 127.0.0.1 -L toybox httpd "$TEST" &
trap "kill $!" EXIT
sleep .25

[ -n "$(wget http://127.0.0.1:65432/ -O - | grep ===)" ] || exit 1

PASS= NOPASS=
for I in root/*/linux-kernel
do
  [ ! -e "$I" ] && continue
  X=$(dirname $I) Y=$(basename $X)
  # Alas KARGS=quiet doesn't silence qemu's bios, so filter output ourselves.
  # QEMU broke -hda because too many people know how to use it, this is
  # the new edgier version they added to be -hda without gratuitous breakage.
  {
    cd $X || continue
    echo === $X
    # When stdin is a tty QEMU will SIGTTOU itself here.
    toybox timeout -i 5 ./run-qemu.sh -drive format=raw,file=../build/test/init.sqf < /dev/null 2>/dev/null
    cd ../..
  } | tee root/build/log/$Y-test.txt | grep '^=== '
  [ "$(grep '^=== ' root/build/log/$Y-test.txt | wc -l)" -eq 4 ] &&
    PASS+="$Y " || NOPASS+="$Y "
done

[ -n "$PASS" ] && echo PASS=$PASS
[ -n "$NOPASS" ] && echo NOPASS=$NOPASS
X="$(ls root | egrep -xv "$(ls root/*/linux-kernel | sed 's@root/\([^/]*\)/linux-kernel@\1@' | tr '\n' '|')build" | xargs)"
[ -n "$X" ] && echo No kernel: $X
