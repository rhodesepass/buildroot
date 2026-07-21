# cedrus-rotate 用户态使用说明

对应补丁:`0028-cedrus-suniv-sdrot-rotate-m2m-node.patch`

内核里多出一个 V4L2 m2m 设备节点(名字 `cedrus-rotate`),用 VE 的 SDROT
单元对整帧做 90/180/270 硬件旋转和水平/垂直镜像。输入输出都是 32x32
tiled NV12(fourcc `ST12`),也就是 cedrus 解码节点吐出来的原生格式。

一次任务 = 喂一个输入 buffer + 一个输出 buffer,硬件转完各还你一个。
和解码共用 VE,内核自动排队,应用层不用管互斥。

## 0. 快速验证(不写代码)

```sh
# 找到节点(card 名为 cedrus-rotate,一般是 video1)
v4l2-ctl --list-devices

# 看控制项在不在
v4l2-ctl -d /dev/video1 -L        # 应有 rotate (int): min=0 max=270 step=90
                                  # 以及 horizontal_flip / vertical_flip (bool)

# 拿一帧解码输出(ST12 裸数据)转 90 度:
v4l2-ctl -d /dev/video1 \
    --set-ctrl rotate=90 \
    --set-fmt-video-out width=368,height=240,pixelformat=ST12 \
    --set-fmt-video width=240,height=368,pixelformat=ST12 \
    --stream-out-mmap --stream-mmap \
    --stream-from in_st12.raw --stream-to out_st12.raw --stream-count 1
```

## 1. 设备发现

别写死 `/dev/video1`。遍历 `/dev/video*`,`VIDIOC_QUERYCAP` 后认
`cap.card == "cedrus-rotate"`(driver 字段仍是 `cedrus`):

```c
int rotate_open(void)
{
    char path[32];
    struct v4l2_capability cap;

    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0)
            continue;
        if (!ioctl(fd, VIDIOC_QUERYCAP, &cap) &&
            !strcmp((char *)cap.card, "cedrus-rotate"))
            return fd;
        close(fd);
    }
    return -1;
}
```

## 2. 尺寸与格式规则(重要)

两端唯一格式:`V4L2_PIX_FMT_SUNXI_TILED_NV12`(`ST12`)。老 SDK 头文件
没有的话自己定义:

```c
#ifndef V4L2_PIX_FMT_SUNXI_TILED_NV12
#define V4L2_PIX_FMT_SUNXI_TILED_NV12 v4l2_fourcc('S', 'T', '1', '2')
#endif
```

尺寸规则:

| 端 | 传什么 |
|---|---|
| OUTPUT(输入图)| **视频原始宽高**(如 360x240)。驱动内部 16 对齐成宏块数,正好等于解码器实际写出的编码尺寸布局 |
| CAPTURE(输出图)| 0/180 度:同输入;**90/270 度:宽高对调** |

不要传解码节点 G_FMT 回来的 32 对齐尺寸(如 384x256)——多出来的
padding 宏块也会被旋转,内容会整体偏移。

驱动算出的 buffer 布局(S_FMT 返回值里能读到,不用自己算,列出来是
为了你解释输出数据):

```
w16    = ALIGN(width, 16)          // 实际生效宽高
h16    = ALIGN(height, 16)
stride = ALIGN(w16, 32)            // bytesperline
luma   平面:  stride * ALIGN(h16, 32) 字节,起始于 0
chroma 平面:  紧随其后(偏移 = stride * ALIGN(h16, 32)),
              占 stride * ALIGN(h16, 64) / 2 字节
sizeimage = 两者之和
```

角度是标准控制项,任务提交前设一次即可,之后每帧沿用:

```c
struct v4l2_control ctrl = { .id = V4L2_CID_ROTATE, .value = 90 };
ioctl(fd, VIDIOC_S_CTRL, &ctrl);
```

### 镜像(HFLIP / VFLIP)

标准布尔控制项,用法同角度:

```c
struct v4l2_control ctrl = { .id = V4L2_CID_VFLIP, .value = 1 };
ioctl(fd, VIDIOC_S_CTRL, &ctrl);
```

倒装机型(如 boe)的视频链就靠 VFLIP 补 Y 分量——DEBE 只翻层坐标不翻
frontend 灌的内容,见 `boe-flip-180.md`。

硬件层面三个控制项归一化成一个操作码(`SDROT_CTRL[2:0]`:0-3 =
0/90/180/270,4 = H 镜像,5 = V 镜像,均板上实测),驱动自动化简:

| 你设的组合 | 实际执行 |
|---|---|
| hflip + vflip | 等效 180°,折进角度 |
| 单镜像 + rotate=180 | 换成另一个方向的单镜像 |
| 单镜像 + rotate=90/270 | **S_CTRL 返回 -EINVAL**(对应 op 6/7 硬件未验证,不放行;真需要时先上板验证 op6/7 再放开) |

镜像不改变宽高,CAPTURE 端尺寸规则只看角度。

## 3. 完整流程(MMAP 版,单帧)

```c
/* 1. 格式 */
struct v4l2_format fmt = {0};

fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;          /* 输入端 */
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SUNXI_TILED_NV12;
fmt.fmt.pix.width  = 360;
fmt.fmt.pix.height = 240;
ioctl(fd, VIDIOC_S_FMT, &fmt);
uint32_t src_size = fmt.fmt.pix.sizeimage;      /* 驱动算好的 */

fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;         /* 输出端,90 度:对调 */
fmt.fmt.pix.width  = 240;
fmt.fmt.pix.height = 360;
ioctl(fd, VIDIOC_S_FMT, &fmt);
uint32_t dst_size = fmt.fmt.pix.sizeimage;

/* 2. 角度 */
struct v4l2_control ctrl = { .id = V4L2_CID_ROTATE, .value = 90 };
ioctl(fd, VIDIOC_S_CTRL, &ctrl);

/* 3. 两端各申请 1 个 buffer 并 mmap */
struct v4l2_requestbuffers req = {0};
req.count = 1; req.memory = V4L2_MEMORY_MMAP;

req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
ioctl(fd, VIDIOC_REQBUFS, &req);
req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(fd, VIDIOC_REQBUFS, &req);

struct v4l2_buffer buf = {0};
buf.index = 0; buf.memory = V4L2_MEMORY_MMAP;

buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
ioctl(fd, VIDIOC_QUERYBUF, &buf);
void *src = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, buf.m.offset);

buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(fd, VIDIOC_QUERYBUF, &buf);
void *dst = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, buf.m.offset);

/* 4. 填输入数据(ST12 裸帧),入队,开流 */
memcpy(src, st12_frame, src_size);

buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
buf.bytesused = src_size;
ioctl(fd, VIDIOC_QBUF, &buf);
buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(fd, VIDIOC_QBUF, &buf);

int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
ioctl(fd, VIDIOC_STREAMON, &type);
type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(fd, VIDIOC_STREAMON, &type);        /* 两端都 on 后任务才启动 */

/* 5. 等结果 */
buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(fd, VIDIOC_DQBUF, &buf);            /* 阻塞到旋转完成 */
if (buf.flags & V4L2_BUF_FLAG_ERROR)
    /* 硬件报错,这帧数据不可用 */;
/* dst 里现在是旋转后的 ST12 帧 */

buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
ioctl(fd, VIDIOC_DQBUF, &buf);            /* 收回输入 buffer */
```

连续转多帧:STREAMON 保持,循环 QBUF/DQBUF 即可,不用重开流。
buffer 时间戳会自动从输入拷到输出。

## 4. 和解码节点零拷贝对接(DMABUF)

解码输出直接喂旋转,不过 CPU:

```c
/* 解码节点侧:把 CAPTURE buffer 导出为 dmabuf fd */
struct v4l2_exportbuffer exp = {0};
exp.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
exp.index = i;                            /* 解码输出 buffer 序号 */
ioctl(dec_fd, VIDIOC_EXPBUF, &exp);       /* 得到 exp.fd */

/* 旋转节点侧:OUTPUT 端 REQBUFS 时改用 DMABUF */
req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
req.memory = V4L2_MEMORY_DMABUF;
ioctl(rot_fd, VIDIOC_REQBUFS, &req);

/* 入队时带上 fd */
buf.type      = V4L2_BUF_TYPE_VIDEO_OUTPUT;
buf.memory    = V4L2_MEMORY_DMABUF;
buf.m.fd      = exp.fd;
buf.bytesused = src_size;
ioctl(rot_fd, VIDIOC_QBUF, &buf);
```

流水线:解码 DQBUF 拿到第 i 个输出 → 旋转节点 QBUF(fd_i) → 旋转
DQBUF → 该帧用完后解码节点再 QBUF(i) 还回去。注意解码 buffer 在旋转
没做完前不能还给解码器复用。

旋转节点的 CAPTURE 端同样可以 EXPBUF 导出,喂给显示链路。

## 5. 注意事项

1. **输出仍是 tiled**。这个节点只解决旋转,不做 untile;显示前该怎么
   转线性还怎么转(转换时用旋转后的宽高和新 stride)。
2. **角度顺逆未验证**。硬件文档缺失,90 是顺时针还是逆时针,拿一帧带
   方向的图试一次;反了就用 270。现成工具:`boe_flip/debe_flip/
   st12rot_probe`(生成 128x96 四角唯一灰度块的 ST12 帧过一遍 m2m,
   自动判定实际变换,用法 `st12rot_probe [angle] [hflip] [vflip]
   [dumpfile]`),HFLIP/VFLIP 的五项组合就是拿它验的。
3. **和解码抢 VE**。任务串行,一帧旋转大约给解码链路加不到 1ms 级的
   占用(相对你 293ms 的解码耗时可忽略),但别在解码卡住排查性能时
   忘了它的存在。
4. **错误恢复**。DQBUF 带 `V4L2_BUF_FLAG_ERROR` 说明硬件报错;连续
   出错或 DQBUF 永久阻塞(中断丢失)的话,两端 STREAMOFF 再 STREAMON
   重来。已知 blob 在每次旋转前会整机复位 VE 而本驱动没做——如果实测
   出现偶发挂死,这是第一嫌疑,回来在 `cedrus_rotate_setup()` 开头
   补 VE 复位。
5. **每个 open 是独立会话**(独立格式/角度/队列),多个进程可各开各的,
   内核排队;但同一会话内一次只有一对 buffer 在硬件里。
6. **给以后改驱动的人**:`SDROT_CTRL` 只有 [2:0] 是安全的。探测时
   实测置 bit4(0x10)会让 SoC 总线硬挂死(串口无响应,只能断电),
   别碰 [2:0] 以外的位。
