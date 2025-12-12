#!/bin/sh
cp board/rhodesisland/epass/scripts/ubinize.cfg "${BINARIES_DIR}/ubinize.cfg"
cd "${BINARIES_DIR}"
fakeroot sh -c "rm -r rootfs"
fakeroot sh -c "rm ubi.img"
mkdir rootfs
fakeroot sh -c "tar xvf rootfs.tar -C rootfs/"

fakeroot sh -c "mkfs.ubifs -x none -F -m 2048 -e 126976 -c 732 -o rootfs_ubifs.img -r rootfs"
fakeroot sh -c "ubinize -o ubi.img -m 2048 -p 131072 -O 2048 -s 2048 ./ubinize.cfg  -v"

rm ubinize.cfg
rm -f rootfs_ubifs.img
fakeroot sh -c "rm -r rootfs"
