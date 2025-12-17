xfel spinand erase 0 0x8000000
xfel spinand splwrite 1024 0 output/images/u-boot-sunxi-with-spl.bin
xfel spinand write 0x20000 output/images/u-boot.img
xfel spinand write 0x100000 output/images/devicetree-0.3.dtb
xfel spinand write 0x120000 output/images/zImage
xfel spinand write 0x680000 output/images/ubi.img
