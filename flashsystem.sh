#!/bin/bash


TARGET="$1"

flash_all() {
    xfel spinand erase 0 0x8000000
    xfel spinand splwrite 1024 0 output/images/u-boot-sunxi-with-spl.bin
    xfel spinand write 0x20000 output/images/u-boot.img
    xfel spinand write 0x100000 output/images/devicetree-0.3.dtb
    xfel spinand write 0x120000 output/images/zImage
    xfel spinand write 0x680000 output/images/ubi.img
}

flash_uboot() {
    xfel spinand splwrite 1024 0 output/images/u-boot-sunxi-with-spl.bin
    xfel spinand write 0x20000 output/images/u-boot.img
}

flash_dt() {
    xfel spinand write 0x100000 output/images/devicetree-0.3.dtb
}

flash_linux() {
    xfel spinand write 0x120000 output/images/zImage
}

case "$TARGET" in
    all)
        flash_all
        ;;
    uboot)
        flash_uboot
        ;;
    dt)
        flash_dt
        ;;
    linux)
        flash_linux
        ;;
    *)
        echo "Usage: $0 [uboot|dt|linux|all]"
        exit 1
        ;;
esac