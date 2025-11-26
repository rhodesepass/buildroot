#!/bin/sh
cp board/rhodesisland/epass/scripts/ubinize.cfg "${BINARIES_DIR}/ubinize.cfg"
cd "${BINARIES_DIR}"
sudo rm -r rootfs
sudo rm ubi.img
mkdir rootfs
sudo tar xvf rootfs.tar -C rootfs/

sudo mkfs.ubifs -x none -F -m 2048 -e 126976 -c 732 -o rootfs_ubifs.img -r rootfs
sudo ubinize -o ubi.img -m 2048 -p 131072 -O 2048 -s 2048 ./ubinize.cfg  -v

rm ubinize.cfg
rm -f rootfs_ubifs.img
sudo rm -r rootfs
