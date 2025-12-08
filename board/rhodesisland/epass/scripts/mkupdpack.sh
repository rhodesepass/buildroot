#!/bin/sh

VERSION=$(git describe --tags --always --dirty)

# Create update package for Electric Pass devices
mkdir -p "${BINARIES_DIR}/flash_pack"
mkdir -p "${BINARIES_DIR}/flash_pack/configs"
mkdir -p "${BINARIES_DIR}/flash_pack/firmware"


cp board/rhodesisland/epass/scripts/binary/* "${BINARIES_DIR}/flash_pack"
cp board/rhodesisland/epass/flash_configs/* "${BINARIES_DIR}/flash_pack/configs"

cp "${BINARIES_DIR}/u-boot-sunxi-with-spl.bin" "${BINARIES_DIR}/flash_pack/firmware"
cp "${BINARIES_DIR}/u-boot.img" "${BINARIES_DIR}/flash_pack/firmware"
mv "${BINARIES_DIR}/devicetree_0.2_flashtool.dts" "${BINARIES_DIR}/flash_pack/firmware/devicetree-0.2.dts"
mv "${BINARIES_DIR}/devicetree_0.3_flashtool.dts" "${BINARIES_DIR}/flash_pack/firmware/devicetree-0.3.dts"
cp "${BINARIES_DIR}/zImage" "${BINARIES_DIR}/flash_pack/firmware"
cp "${BINARIES_DIR}/ubi.img" "${BINARIES_DIR}/flash_pack/firmware"

cd "${BINARIES_DIR}"
rm -f flash_pack_*.zip
zip -r flash_pack.zip flash_pack/*
mv flash_pack.zip "flash_pack_$VERSION.zip"

rm -r flash_pack/

