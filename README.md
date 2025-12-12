# 明日方舟 电子通行证 Buildroot SDK

基于aodzip老师的buildroot-tiny200 发行版，更改rootfs文件系统为UBIFS，更新上游U-boot，并进行硬件解码相关的修补。

本buildroot是“三合一”buildroot，可以生成Kernel、U-boot、rootfs。

默认用户名：root 默认密码：toor

电子通行证文件相关位于：buildroot-epass/board/rhodesisland/epass目录下

## 如何构建系统

### 安装工具包
``` shell
sudo apt install wget unzip build-essential git bc swig libncurses-dev libpython3-dev libssl-dev mtd-utils
sudo apt install python3-distutils fakeroot
```

### 克隆本仓库
```shell
git clone https://github.com/inapp123/buildroot-epass
```

### 应用 defconfig

**请注意：应用defconfig会覆盖你之前的所有的配置！！**

**一般来说只需要应用一次**

```shell
cd buildroot-epass
make rhodesisland_epass_defconfig
```

### 构建
```shell
make
```
构建的结果在output/images中。其中flash_pack_xxxx.zip是带有Windows刷机工具的更新包，可以直接交付。

### 重新构建内核及设备树
```shell
./rebuild-kernel.sh
```

### 重新构建U-boot
```shell
./rebuild-uboot.sh
```

### 直接烧录系统

需要先安装[XFEL](https://github.com/xboot/xfel)

```shell
./flashsystem.sh
```

# Buildroot Package for Allwinner SIPs
Opensource development package for Allwinner F1C100s & F1C200s

## Driver support
Check this file to view current driver support progress for F1C100s/F1C200s: [PROGRESS-SUNIV.md](PROGRESS-SUNIV.md)

Check this file to view current driver support progress for V3/V3s/S3/S3L: [PROGRESS-V3.md](PROGRESS-V3.md)

## Install

### Install necessary packages
``` shell
sudo apt install wget unzip build-essential git bc swig libncurses-dev libpython3-dev libssl-dev
sudo apt install python3-distutils
```

### Download BSP
**Notice: Root permission is not necessery for download or extract.**
```shell
git clone https://github.com/aodzip/buildroot-tiny200
```

## Make the first build
**Notice: Root permission is not necessery for build firmware.**

### Apply defconfig
**Caution: Apply defconfig will reset all buildroot configurations to default values.**

**Generally, you only need to apply it once.**
```shell
cd buildroot-tiny200
make widora_mangopi_r3_defconfig
```

### Regular build
```shell
make
```

## Speed up build progress

### Download speed
Buildroot will download sourcecode when compiling the firmware. You can grab a **TRUSTWORTHY** archive of 'dl' folder for speed up.

### Compile speed
If you have a multicore CPU, you can try
```
make -j ${YOUR_CPU_COUNT}
```
or buy a powerful PC for yourself.

## Flashing firmware to target
You can flash a board by Linux (Recommended) or Windows system.
### [Here is the manual.](flashutils/README.md)

## Helper Scripts
- rebuild-uboot.sh: Recompile U-Boot when you direct edit U-Boot sourcecode.
- rebuild-kernel.sh: Recompile Kernel when you direct edit Kernel sourcecode.
- emulate-chroot.sh: Emulate target rootfs by chroot.
