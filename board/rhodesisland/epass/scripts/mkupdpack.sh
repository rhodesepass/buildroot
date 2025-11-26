#!/bin/sh
# Create update package for Electric Pass devices
mkdir -p "${BINARIES_DIR}/flash_pack"
cp board/rhodesisland/epass/scripts/binary/* "${BINARIES_DIR}/flash_pack"
cp "${BINARIES_DIR}/u-boot-sunxi-with-spl.bin" "${BINARIES_DIR}/flash_pack/"
cp "${BINARIES_DIR}/u-boot.img" "${BINARIES_DIR}/flash_pack/"
cp "${BINARIES_DIR}/devicetree.dtb" "${BINARIES_DIR}/flash_pack/"
cp "${BINARIES_DIR}/zImage" "${BINARIES_DIR}/flash_pack/"
cp "${BINARIES_DIR}/ubi.img" "${BINARIES_DIR}/flash_pack/"

cd "${BINARIES_DIR}"
rm -f flash_pack.zip
zip -r flash_pack.zip flash_pack/*

rm -r flash_pack

