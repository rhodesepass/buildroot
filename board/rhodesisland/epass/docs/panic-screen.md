# Panic 上屏 (srgn_panic)

内核 panic / oops 时，把内核日志尾部连同一只 ASCII 企鹅画到面板上，而不是让屏幕
定格在 app 的最后一帧。

相关补丁：`patch/linux/0033-sun4i-panic-screen.patch`
内核选项：`CONFIG_DRM_SUN4I_PANIC=y`（`select FONT_SUPPORT` + `FONT_6x10`）

字体是 **6x10** 而不是常见的 8x16 —— 360x640 那块屏上是 60×64 格 vs 45×40 格，
日志区从约 1400 字符涨到约 3100。`lib/fonts` 里宽度 ≤8 的字模一律一字节一行、
最左像素在 bit7，blit 不关心选哪个。`FONT_6x10` 原本 `depends on FONTS`
（而 `FONTS` 又 `depends on FRAMEBUFFER_CONSOLE`），补丁照 `FONT_8x16` 的写法把它
改成 `bool "..." if FONTS` + `depends on !SPARC`，就能被直接 `select`。

> 还想再多？上游 6.12 的 QR（`CONFIG_DRM_PANIC_SCREEN_QR_CODE`）把 kmsg 过 zlib
> deflate 再塞进 QR，载荷上限约 2953 字节 ≈ 8~12KB 日志。但编码器是 **Rust**
> 写的（`drm_panic_qr.rs`），5.4 用不了，要自己写 C 版 + 常驻 zlib workspace。

## 原理

上游 `drm_panic`（6.10+）让驱动记住"当前正在扫描的那块 buffer"。**这里不这么做**：
那个记账本身就可能是错的 —— app 双缓冲、换层、重启，任何软件侧镜像都可能比硬件慢一拍，
画到上一帧那块去，屏幕上什么都看不见。

所以 panic 时**直接问 DEBE**：层寄存器就是唯一真相，哪些层开着、多大、各自从哪个物理
地址取数。这颗 SoC 没有 IOMMU，那个物理地址一定是连续的 lowmem，`phys_to_virt()`
直接落在上面。

1. 读 `MODCTL`，挑面积最大、每像素 ≥ 2 字节的使能层（跳过 `VDOEN`/`YUVEN` 的
   frontend 视频层，它没有自己的 RGB buffer）
2. 从 `LAYFB_L32ADD` / `LAYFB_H4ADD`（寄存器单位是 **bit**）算出地址，
   `LAYSIZE` 给宽高，`LAYLINEWIDTH` 给 stride 和符号
3. **把总线地址翻译成物理地址**（见下），往那块内存里画 RGB565
4. 刷 cache，然后把该层 `ATTCTL_REG1` 的格式强改成 `RGB565`、`LAYLINEWIDTH` 改成
   `width*2*8`、清掉全局 alpha、`LAYCOOR` 归零、`LAYSIZE` 收到 `DISSIZE` 以内，
   其余层的 `LAY_EN` 和 `HWC_EN` 全关，`REGBUFFCTL` 锁存

### 地址是 DMA 总线地址，不是物理地址

DT 里没有 `interconnects`，所以 `sun4i_backend_bind()` 走的是
`drm->dev->dma_pfn_offset = PHYS_PFN_OFFSET` 那条路 —— **DEBE 眼里 DRAM 从 0 开始**，
CPU 眼里从 `0x80000000` 开始。板上实测 `lay1 pa=0x01c50000`，真实物理地址是 `0x81c50000`。

唯一的例外是 U-Boot 留下的 splash 层：U-Boot 不知道有这回事，写的是**裸 CPU 地址**，
所以 `LAYFB_H4ADD` 里只有它那个 nibble 是非零的（`h4add=0x00000004`）。
驱动先按 `dma_pfn_offset` 翻译，落不进 lowmem 就按原值再试一次，取能落到真实内存的那个。

### 位置也要归位

app 可以把 overlay 停在任意位置 —— 板上实测那块 RGB565 层的 `coor=0x03840000`（y=900）。
既然整层接管，`LAYCOOR` 清零、`LAYSIZE` 收到面板尺寸以内，否则字滚出屏幕跟没画一样。

**格式只会往小改**，所以 ARGB8888 的 buffer 装 RGB565 绰绰有余，越不出界 —— 这是
"随便挑一层往里画"能成立的关键。

panic 路径上因此没有任何软件状态依赖：不追踪、不持 GEM 引用、不在 atomic commit 里
挂钩子、不分配内存，几何和格式全部现场读，没有一处写死的分辨率。

唯一的例外是 **C8 调色板层**：一字节每像素没有余量改成 RGB565，只能按原样用
（保持硬件原本的 stride），并把 DEBE 调色板 SRAM 读回来挑最暗/最亮两个索引当背景/前景
—— **不改表**，改表会破坏还留在屏上的其它内容。

## 触发

```sh
echo 1 > /sys/kernel/srgn_panic/test   # panic
echo 2 > /sys/kernel/srgn_panic/test   # BUG()
echo 3 > /sys/kernel/srgn_panic/test   # NULL 解引用,走 oops 路径
echo 4 > /sys/kernel/srgn_panic/test   # WARN,应当【不】上屏
```

`4` 那档是阴性对照：`WARN` 不会调用 kmsg dumper，屏幕必须毫无变化。

## 画面读法

```
┌──────────────────────────────────────────┐
│ KERNEL PANIC                             │  ← 红底(accent)反色标题
│     .--.                                 │
│    |o_o |     5.4.99                     │  ← init_utsname()->release
│    |:_/ |     #1 PREEMPT <build date>    │  ← ->version,用来挑对 System.map
│   //   \ \    DEBE layer 2  720x1280 ... │  ← 抢到的是哪一层、多大
│  (|     | )   hold power to reset        │
│ /'\_   _/`\                              │
│ \___)=(___/                              │
│                                          │
│ [   12.345678] Unable to handle kernel   │  ← kmsg 尾部,不够宽就硬折行
│ [   12.345678] PC is at ...              │
│ ...                                      │
└──────────────────────────────────────────┘
```

屏幕窄于 32 列时企鹅会被跳过，只排文字。日志区从缓冲区**末尾**往回填，
所以最后一行（panic 原因和回溯尾部）一定可见。

`bootargs` 里 `panic=0`，画面会一直停到断电。串口同时有完全相同的内容，
可以逐行对账 —— 这是最快的正确性判据。

## 排查

**屏幕毫无变化**
- 串口有没有 `srgn_panic: no usable DEBE layer` —— 说明 `MODCTL` 里一个合格的层都没有
  （全被 blank 了，或者只剩 frontend 喂的 NV12 视频层）
- `dmesg | grep srgn_panic` 看 dumper 有没有注册
- 时钟一旦被 `clk_disable_unused` 或 crtc disable 关掉，panic 里进不了 CCF（全是 mutex），
  面板进了 sleep 也发不了 SPI 唤醒命令。**灭屏状态下 panic 无解**，这是已知边界。

**画面位置不对 / 只占屏幕一角**
抢到的层不是全屏的（`LAYCOOR` 有偏移，或者 `LAYSIZE` 小于面板）。标题行右边那句
`DEBE layer N WxH` 就是用来对账的 —— 它告诉你抢到了哪一层、多大。

**字是倒的**
说明 yflip 板子的坐标处理被人改错了。正常情况下**不需要**任何特殊处理：负 stride
（patch 0031）加面板 0xC7 SDIR 合起来是整屏 180°，app 用正常坐标画进 buffer，
我们也用正常坐标画，出来就是正的。

**日志是空的 / 只有几行**
`kmsg_dump_rewind_nolock()` 应该已经绕开了 `clear_seq` 的问题（`dmesg -c` 之后仍能拿到
完整 ring buffer）。如果确实很短，看 `CONFIG_LOG_BUF_SHIFT`（当前 15 = 32KB）。

**回溯只有裸地址**
`CONFIG_KALLSYMS` 被关了。开着的话应当是 `sun4i_backend_commit+0x1c/0x40` 这种。
真关了就拿 `output/build/linux-5.4.99/System.map` 手工二分查地址所在的符号区间，
注意 `init_utsname()->version` 里的编译时间要和这份 System.map 对得上。

**画完屏还是自己重启了**
看门狗。驱动画完会裸写 `WDT_MODE = 0` 关掉它（`0x01c20ca0 + 0x18`），如果还是复位，
检查 `panic=` 是不是没生效 —— 实际 `bootargs` 通常来自设备上的 `env.txt`
（NAND 0xE0000 或 SD FAT），仓库里的 `uboot.env` 只是兜底。

## 覆盖不了的情况

- **纯死锁**（IRQ 也关了的死循环、总线挂死）：ARMv5 没有 NMI，
  `HARDLOCKUP_DETECTOR` 在这个架构上根本不可选；`sunxi_wdt` 没有 pretimeout 中断，
  复位后直接回 BROM。内核侧无解。
  `CONFIG_SOFTLOCKUP_DETECTOR` / `DETECT_HUNG_TASK` 能覆盖"IRQ 还活着"的那类卡死
  （实际遇到的绝大多数），转成 panic 后复用本路径。
- **跨重启留存**：没有 pstore/ramoops，panic 日志不过重启。要做的话是独立的一小块
  `reserved-memory`（避开 splash `0x81800000` 和 CMA），和本方案正交。
