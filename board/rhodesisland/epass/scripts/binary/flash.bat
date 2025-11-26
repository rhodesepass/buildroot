@echo off

echo 电子通行证烧录程序。
echo 罗德岛工程部 (c)1097
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo 烧录将会清除设备内所有数据，请提前备份好资源文件！
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo .
echo 请按住设备开关旁的按钮（FEL按钮），并打开设备电源，
echo 等待几秒钟后松开FEL按钮，将设备连接上电脑。
PAUSE
echo 准备烧录。请确认下方输出为“found W25N01......”

xfel spinand

PAUSE

xfel spinand erase 0 0x8000000
xfel spinand splwrite 1024 0 u-boot-sunxi-with-spl.bin
xfel spinand write 0x20000 u-boot.img
xfel spinand write 0x100000 devicetree.dtb
xfel spinand write 0x120000 zImage
xfel spinand write 0x680000 ubi.img

echo 烧录完成！请断开设备电源，拔掉数据线，然后重新上电启动设备。
PAUSE