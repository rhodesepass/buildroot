# BOE 倒装屏整机 180° 方案

对应改动:内核 `0031-sun4i-backend-scanout-yflip.patch`、U-Boot
`0019-srgn-splash-scanout-yflip.patch`、`devicetree/linux/screen/boe.dts`、
cedrus 侧见 `cedrus-rotate-usage.md`。

## 0. 为什么要做、为什么这么做

BOE 机型的屏(mostima 360x640,ST7701S)在整机里是倒着装的,画面需要
旋转 180° 才正。三条路都试过:

1. **面板侧整翻(改 init 序列)**——走不通。这块屏的 GIP 时序表不对称,
   gate 反扫(GS)需要配套重排 E5/E8/ED 整套 demux 表,厂商没给对称配置,
   盲调风险大。
2. **CPU/GPU 软转**——F1C 没 GPU,NEON 转 360x640@60 白吃 CPU(官方
   Tina disp v1 就是这么干的),不接受。
3. **扫描端硬件翻(本方案)**——DEBE/TCON/TVE 手册里都没有 flip 位,
   但 DEBE 的行地址是"基址 + 行号 × LAYLINEWIDTH"累加出来的,而
   LAYLINEWIDTH 寄存器吃补码负值(无文档行为,本片实测成立):地址指到
   最后一行、行宽写负,扫描就逐行倒序。零带宽、零 CPU、零延迟。

180° = X 镜像 ∘ Y 镜像,两个分量分开做:

| 分量 | 谁做 | 机制 |
|---|---|---|
| Y(上下) | DEBE(UI/C8 层) | 负 stride 倒扫 + 层坐标映射 |
| Y(视频层内容) | VE SDROT | V4L2_CID_VFLIP,见 cedrus-rotate-usage.md |
| Y(HWC 电池层) | DEBE HWC(patch 0022) | 位置映射到底部 + pattern 行倒序 |
| X(左右) | 面板 ST7701S | init 序列 BK0 `0xC7 0x04`(SDIR,源极反扫) |
| splash | U-Boot | 与内核同机制,读同一个 DT key |

HWC 电池层要单列一行:它是 alpha blender0 上的独立硬件块,layer 的负
stride 倒扫和层坐标映射都作用不到它,面板 SDIR 又只翻 X,所以整机
180° 的 Y 分量得在 `bat_hwc_apply()` 里自己补(读同一个 `backend->yflip`)。

X 镜像敢交给面板是因为 SDIR 只反转源极输出顺序,不碰 GIP;实测无副作用。
Y 交给 SoC 是因为 GIP 不对称(上面第 1 条)。

## 1. DEBE 负 stride 的原理与边界

- `LAYFB_L32ADD`(0x850+n*4)写**最后一行**的位地址:
  `(基址 + (H-1)×pitch) × 8`;
- `LAYLINEWIDTH`(0x840+n*4)写 `-(pitch×8)` 的 32 位补码;
- 写 `REGBUFFCTL`(0x870)= 3 手动 latch 生效。

行地址累加器只有 32 位,`H4ADD` 是静态高位不参与进位(板上实测),
负值累加在低 32 位干净回绕。另外 DEBE 用 DRAM 相对寻址:字节地址 ×8
后 0x80000000 恰好溢出为 0,寄存器里存的等效于 DRAM 偏移的位地址,
64MB 内不会越界。

**只翻内容不够,坐标也要翻。** 全屏层翻内容就完事(所以 splash 简单),
但非全屏层(overlay、脏矩形部分刷新)内容倒了、位置还在原地,整屏就
错位。所以每层的 LAYCOOR 的 Y 要绕水平中线映射:

```
y' = vdisplay - y - h
```

内容翻转只作用于 BE 直接 DMA 的 RGB/C8 层;packed-YUV 层跳过;
frontend 灌进来的视频层内容由 SDROT 预翻(app 负责设 VFLIP),
**坐标映射对所有层生效**(包括视频层)。

## 2. 设备树落地(boe.dts)

开关是 be0 节点的布尔属性,倒装机型的 screen overlay 里设置:

```dts
fragment@3 {
    target = <&be0>;
    __overlay__ {
        srgn,scanout-yflip;
    };
};
```

X 镜像在同文件的 init 序列里,Command2 BK0 段(0xC2 之后):

```dts
/* x flip */
ST7701INIT_WRITE_C8_D8 0xC7 0x04
```

内核(patch 0031)和 U-Boot(patch 0019)读的是同一个
`srgn,scanout-yflip`,所以一个 key 同时管 splash 和 UI;不带这个 key
的机型(hsd、papyrus 等)行为完全不变。

app 判断整机是否倒装(决定视频要不要设 VFLIP):

```sh
[ -e /proc/device-tree/soc/display-backend@1e60000/srgn,scanout-yflip ]
```

## 3. 内核实现要点(patch 0031)

`sun4i_backend_update_layer_buffer()`:yflip 时地址加 `(h-1)×pitch`、
linewidth 取负;`sun4i_backend_update_layer_coord()`:LAYCOOR 的 Y 做
中线映射。bind 时读一次 DT key 存在 `backend->yflip`。

**踩过的坑,改这段代码前必读:**

> 源高度必须取 `state->src_h >> 16`,**绝对不能用**
> `drm_rect_height(&state->src)`。`state->src` 这个矩形只有调用
> `drm_atomic_helper_check_plane_state()` 的驱动才会被填,sun4i 的
> plane 没有 atomic_check,它恒为 0——h=0 会让扫描起点变成"基址前
> 一行"再倒扫 640 行,整屏扫的是 fb **前面**的无关内存。症状极具
> 欺骗性:寄存器逐项验算全对(错位量恰好是整页数,反推的伪基址仍
> 页对齐)、屏幕显示"结构化的内存垃圾"。定位办法:
> `echo 0x1f > /sys/module/drm/parameters/debug`,拿 dmesg 里
> "Setting buffer address"(gem 真基址)与 L32ADD 对账。

## 4. U-Boot splash(patch 0019)

`srgn_debe_setup()` 增加 yflip 参数:fb 指针加 `(yres-1)×xres×2`、
stride 取负。`do_srgn_splash` 用
`fdt_node_offset_by_compatible(..., "allwinner,suniv-f1c100s-display-backend")`
找节点读 key。splash 是全屏单层,无需坐标映射。

内核侧的 live handover(patch 0013)只透传寄存器现场,与负 stride
兼容,不需要改。

## 5. 调试工具

都在源码仓 `boe_flip/debe_flip/`(板上放 /root):

- `drmflip`:起一张静态不对称测试图(顶红/底蓝/绿渐变/白对角线)并
  持住 DRM master,专给肉眼判定翻转/花屏;`drmflip_dbg` 版会打印
  mmap offset 和 VA 读回值。
- `debe_flip.sh on|off|dump [layer]`:devmem 手动翻转/恢复任意层,
  自己从寄存器读参数算末行,不依赖内核 yflip 路径——对照实验用。
- `drmprobe`:CREATE_DUMB + mmap 写 magic 后持住,配合全内存搜索
  定位 dumb buffer 的真实物理页。

## 6. 验证状态(2026-07-22,BOE 板)

- splash 正立 ✓(U-Boot 负 stride)
- app UI 整机 180° 正立、内容清晰 ✓(内核 yflip + 面板 SDIR)
- 层坐标映射 ✓(overlay 位置正确)
- 视频层 VFLIP:内核/驱动侧就绪,app 集成待做
- HWC 电池层 Y 翻转(patch 0022):代码就绪,**未上板验证**
