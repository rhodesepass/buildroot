# ePass NAND storage layout

```
0x0000000 ~ 0x00E0000 U-Boot SPL + U-boot
0x00E0000 ~ 0x0100000 boot environment (1 eraseblock, 0x20000-aligned)
0x0100000             DTBs and splash FIT (1 MiB logical slot)
0x0200000             Kernel FIT nominal start
0x0700000 ~ 0x2300000 rootfs (28 MiB): ubi0, single 24 MiB "rootfs" volume,
                      read-only UBIFS (overlay lower layer)
0x2300000 ~ 0x8000000 data (93 MiB): ubi1, "rootfs_data" volume, writable
                      UBIFS (overlay upper layer + workdir)
```

these layout is shared between U-Boot and Kernel.

The two FIT images are packed into one boot.itb for DFU compatibility. The
image ends after kernel.itb instead of being padded to 6 MiB, leaving the rest
of the boot partition as bad-block reserve. If DFU skips bad blocks before the
kernel FIT, its physical start moves by whole eraseblocks; U-Boot scans from
the nominal offset to find it.

The root filesystem is an overlayfs assembled by /preinit (init=/preinit):
the rootfs volume is mounted read-only as the lower layer, and upper/work
directories live on the data volume. The DFU `rootfs` alt only rewrites the
rootfs MTD partition (`rundfu` erases 0x100000~0x2300000), so reflashing
preserves user data. If the data partition is unformatted or corrupted,
preinit rebuilds it (ubiformat + ubimkvol) and loses its contents.

Partitions come from fixed-partitions in both
devicetree/linux/base/epass_common.dtsi (kernel) and
devicetree/uboot/suniv-f1c100s-generic.dts (U-Boot / DFU). Keep those two in
sync when changing the layout, and match ubinize-rootfs.cfg / mkfs.ubifs -c
in scripts/buildimage.sh to the rootfs volume size.

# ePass SD Card

* raw offset 8 KiB: u-boot-sunxi-with-spl.bin (same place DFU writes with
  `uboot raw 0x10 0x7f0`).
* mmcblkxp1: 8 MiB FAT16 boot partition, 1 MiB offset.
  * dtbs.itb (device trees and splash)
  * kernel.itb (kernel)
  * env.txt
* mmcblkxp2: 40 MiB ext4 rootfs container.
  * Contains rootfs.squashfs (the read-only system, overlay lower layer)
    plus a minimal busybox + preinit environment to bootstrap it.
  * The flashed image is only squashfs size + ~17 MiB to keep full-speed USB
    DFU transfers short; S00sdsetup grows it to the full partition with
    resize2fs on first boot.
* mmcblkxp3: 256 MiB ext4 "data" partition (overlay upper layer + workdir).
  * Formatted by preinit when it has no filesystem (first boot, or after
    corruption); reflashing rootfs (p2) leaves it untouched.
* mmcblkxp4: FAT "share" partition covering the rest of the card.
  * Formatted by S00sdsetup on first boot, and mounted at /sd (same path as
    the external SD card when booting from NAND).

As on NAND, the rootfs is an overlayfs assembled by /preinit inside the p2
container: it loop-mounts rootfs.squashfs as the lower layer and uses p3 as
the upper layer.

There are two ways to flash an SD card:

* **xfel + DFU** (`flash.py` → `flash_sd`): U-Boot's `sdflash` writes the MBR
  sized to the actual card (share is `size=-`), DFU downloads u-boot / boot /
  rootfs, and `fatwrite` stores an env.txt with the device_rev/screen passed
  to flash.py.
* **dd the whole-card image** (`output/images/sd_image.img`, built by
  `gensdimage.py`):

  ```
  dd if=sd_image.img of=/dev/sdX bs=1M   # the whole card, not a partition
  ```

  The image is ~306 MiB: MBR + u-boot + boot partition + rootfs container +
  the empty data partition + 1 MiB of zeros at the start of share (wipes
  stale filesystem signatures so S00sdsetup reformats it). Since the card
  size is unknown at build time, the MBR entry for share claims to extend to
  the 2 TB MBR addressing limit; the kernel truncates it to the real end of
  the card at boot, and S00sdsetup rewrites the on-disk size field to the
  real value on first boot so the partition table also looks sane in card
  readers.

  The image carries a default env.txt (device_rev=0.6, screen=hsd); mount the
  boot partition on a PC and edit it for other hardware (see README.txt on
  the boot partition).

# The `Mostima_` flashing mailbox

`flash.py` runs `xfel ddr`, drops a small header plus an env fragment at DRAM
0x80000000, then loads and executes u-boot.bin at 0x81700000. The `mstmchk`
command reads that header; `load1env` in uboot.env dispatches on it and clears
the magic afterwards so a plain reboot does not re-enter flashing.

| off  | size | field                                                      |
|------|------|------------------------------------------------------------|
| 0x00 | 8    | magic `Mostima_`                                           |
| 0x08 | 1    | boot type: 1 = NAND flow, 2 = SD flow (`${mstm_ret}`)      |
| 0x09 | 1    | flags (`${mstm_flags}`, hex)                               |
| 0x0a | 2    | padding                                                    |
| 0x0c | 4    | u32 LE byte count of the env payload, incl. trailing NUL   |
| 0x10 | n    | env text for `env import -t`, e.g. `device_rev=`/`screen=` |

Flag bits, handled by `mstm_prep` (NAND) and `mstm_prep_sd` (SD):

* **0x01 — wipe user data.** Normal flashing deliberately leaves the user data
  partition alone, so a full reflash is a data-preserving upgrade. With this
  bit the NAND `data` partition is erased outright; on SD the first MiB of p3
  is zeroed, which is enough to lose the ext4 superblock and make preinit
  reformat it on the next boot. The SD `share` partition (p4) is not touched.
* **0x02 — full-chip force erase and bad block rescan (NAND only, ignored on
  SD).** Runs `mtd erase.dontskipbad` over the entire chip, wiping factory bad
  block markers along with everything else, then rebuilds the bad block table
  from the erase results: only a block that actually fails to erase is marked
  bad again. This needs patch 0020, without which `.dontskipbad` is a no-op on
  SPI-NAND. It implies 0x01. Note that this also erases the u-boot and bootenv
  partitions — u-boot is running from DRAM at that point and the DFU run
  rewrites both, but an interruption in between means recovery over FEL only.

In flash.py the bits are `FLAG_WIPE_USERDATA` / `FLAG_NAND_SCRUB`, passed as
the `flags=` argument of `flash_nand()` / `flash_sd()`.
