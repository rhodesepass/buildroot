import subprocess
import struct
import time

# 信箱 header 的 flags 字节(offset 0x09), U-Boot 侧由 mstmchk 导出成 ${mstm_flags}
FLAG_WIPE_USERDATA = 0x01  # 刷机时一并清掉用户数据分区(NAND data / SD p3)
FLAG_NAND_SCRUB = 0x02  # NAND 专用: 全片不跳坏块强制擦除, 之后重新发现坏块


def dfu_device_present():
    try:
        result = subprocess.run(["dfu-util", "-l"], capture_output=True, text=True)
        return "Found DFU: [1f3a:1010]" in result.stdout
    except Exception:
        return False


def wait_for_dfu():
    print("等待设备重启进入DFU模式...", end="", flush=True)
    while True:
        print(".", end="", flush=True)
        if dfu_device_present():
            print("\n抓到了!")
            break
        time.sleep(0.5)


def make_bootinfo(env_payload, boot_type, flags=0):
    with open(".bootinfo.txt", "wb") as f:
        f.write(b"Mostima_")
        f.write(bytes([boot_type, flags, 0, 0]))
        f.write(struct.pack("<I", len(env_payload)))
        f.write(env_payload)


def xfel_boot():
    subprocess.run(["xfel", "ddr"])
    subprocess.run(["xfel", "write", "0x80000000", ".bootinfo.txt"])
    subprocess.run(["xfel", "write", "0x81700000", "output/images/u-boot.bin"])
    subprocess.run(["xfel", "exec", "0x81700000"])


def flash_nand(rev, screen, files, flags=0):
    env_payload = (
        f"device_rev={rev}\n"
        f"screen={screen}\n"
    ).encode("utf-8") + b"\x00"

    make_bootinfo(env_payload, 0x01, flags)
    xfel_boot()

    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "uboot", "-D", files["uboot"]]
    )
    print("烧录uboot分区完成，等待2秒后开始烧录boot分区...")
    time.sleep(2)
    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "boot", "-D", files["boot"]]
    )
    print("烧录boot分区完成，等待2秒后开始烧录rootfs分区...")
    time.sleep(2)
    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "rootfs", "-D", files["rootfs"]]
    )
    # -R(USB reset) 不再让 u-boot 退出 dfu，只有 DFU_DETACH 才行；-e 与 -D 的
    # mode 互相覆盖，必须单独跑一次
    print("全部分区烧录完成，通知设备退出 DFU...")
    subprocess.run(["dfu-util", "-d", "1f3a:1010", "-a", "rootfs", "-e"])


def flash_sd(rev, screen, files, flags=0):
    env_payload = (
        f"device_rev={rev}\n"
        f"screen={screen}\n"
    ).encode("utf-8") + b"\x00"

    make_bootinfo(env_payload, 0x02, flags & FLAG_WIPE_USERDATA)
    xfel_boot()

    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "uboot", "-D", files["uboot"]]
    )
    print("烧录 SD u-boot 完成，等待2秒后开始烧录 boot 分区...")
    time.sleep(2)
    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "boot", "-D", files["boot"]]
    )
    print("烧录 SD boot 分区完成，等待2秒后开始烧录 rootfs 分区...")
    time.sleep(2)
    wait_for_dfu()
    subprocess.run(
        ["dfu-util", "-d", "1f3a:1010", "-a", "rootfs", "-D", files["rootfs"]]
    )
    # -R(USB reset) 不再让 u-boot 退出 dfu，只有 DFU_DETACH 才行；-e 与 -D 的
    # mode 互相覆盖，必须单独跑一次
    print("全部分区烧录完成，通知设备退出 DFU...")
    subprocess.run(["dfu-util", "-d", "1f3a:1010", "-a", "rootfs", "-e"])


if __name__ == "__main__":
    # flags 可选: FLAG_WIPE_USERDATA | FLAG_NAND_SCRUB, 不传就是保数据升级
    # flash_nand(
    #     "p0.1",
    #     "boe_035",
    #     {
    #         "uboot": "output/images/u-boot-sunxi-with-nand-spl.bin",
    #         "boot": "output/images/boot.itb",
    #         "rootfs": "output/images/rootfs_ubi.img",
    #     },
    #     flags=FLAG_WIPE_USERDATA,
    # )

    flash_sd(
        "p0.1",
        "boe_035",
        {
            "uboot": "output/images/u-boot-sunxi-with-spl.bin",
            "boot": "output/images/bootfs.vfat",
            "rootfs": "output/images/rootfs.ext4",
        },
        flags=FLAG_WIPE_USERDATA,
    )
