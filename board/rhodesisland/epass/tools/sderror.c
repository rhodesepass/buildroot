// sderror —— SD 启动模式下拔卡后的全屏 "SD ERROR" 提示。
// 运行环境是 rootfs 已死、只剩 tmpfs 的系统(由 sdwatch 从 /tmp 拉起),因此:
//   - 必须静态链接,不碰动态 loader
//   - 不用 libdrm(.so 在死 rootfs 上),KMS 全部裸 ioctl,头文件只用内核 uapi
// 显示后永不退出,持有 DRM fd(master + framebuffer 引用)常显直到断电。

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

#define DEV       "/dev/dri/card0"
#define MSG       "SD ERROR"
#define MSG_LEN   8
#define BG        0xffcc0000u  // 红底
#define FG        0xffffffffu  // 白字
#define MAX_IDS   8            // 单 TCON 单面板,资源个数远小于此

// 8x8 点阵(font8x8_basic 摘录),每字节一行,bit0 为最左像素
static const uint8_t font_S[8] = { 0x1e, 0x33, 0x07, 0x0e, 0x38, 0x33, 0x1e, 0x00 };
static const uint8_t font_D[8] = { 0x1f, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1f, 0x00 };
static const uint8_t font_E[8] = { 0x7f, 0x46, 0x16, 0x1e, 0x16, 0x46, 0x7f, 0x00 };
static const uint8_t font_R[8] = { 0x3f, 0x66, 0x66, 0x3e, 0x36, 0x66, 0x67, 0x00 };
static const uint8_t font_O[8] = { 0x1c, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x00 };
static const uint8_t font_sp[8] = { 0 };

static const uint8_t *glyph(char c)
{
	switch (c) {
	case 'S': return font_S;
	case 'D': return font_D;
	case 'E': return font_E;
	case 'R': return font_R;
	case 'O': return font_O;
	default:  return font_sp;
	}
}

static void log_kmsg(const char *msg)
{
	int fd = open("/dev/kmsg", O_WRONLY);
	if (fd >= 0) {
		write(fd, msg, strlen(msg));
		close(fd);
	}
}

static void render(uint8_t *map, uint32_t pitch, uint32_t w, uint32_t h)
{
	for (uint32_t y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(map + y * pitch);
		for (uint32_t x = 0; x < w; x++)
			row[x] = BG;
	}

	// 按宽度铺满 8 字符再打 8 折留边;大颗粒像素块,醒目
	uint32_t scale = w / (MSG_LEN * 8) * 8 / 10;
	if (scale == 0)
		scale = 1;
	uint32_t tw = MSG_LEN * 8 * scale, th = 8 * scale;
	uint32_t ox = (w - tw) / 2, oy = (h > th) ? (h - th) / 2 : 0;

	for (int i = 0; i < MSG_LEN; i++) {
		const uint8_t *g = glyph(MSG[i]);
		for (int gy = 0; gy < 8; gy++) {
			for (int gx = 0; gx < 8; gx++) {
				if (!(g[gy] >> gx & 1))
					continue;
				uint32_t x0 = ox + (i * 8 + gx) * scale;
				uint32_t y0 = oy + gy * scale;
				for (uint32_t dy = 0; dy < scale && y0 + dy < h; dy++) {
					uint32_t *row = (uint32_t *)(map + (y0 + dy) * pitch);
					for (uint32_t dx = 0; dx < scale && x0 + dx < w; dx++)
						row[x0 + dx] = FG;
				}
			}
		}
	}
}

// 完整跑一遍 open→modeset→render→SETCRTC。任何一步失败返回 -1(fd 已关)。
// app 死透前占着 DRM master,SETCRTC 会 EACCES,靠外层重试。
static int try_display(void)
{
	int fd = open(DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -1;

	uint32_t crtcs[MAX_IDS], conns[MAX_IDS], encs[MAX_IDS];
	struct drm_mode_card_res res = {
		.crtc_id_ptr      = (uintptr_t)crtcs,
		.connector_id_ptr = (uintptr_t)conns,
		.encoder_id_ptr   = (uintptr_t)encs,
		.count_crtcs      = MAX_IDS,
		.count_connectors = MAX_IDS,
		.count_encoders   = MAX_IDS,
	};
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 ||
	    res.count_connectors == 0 || res.count_connectors > MAX_IDS ||
	    res.count_crtcs > MAX_IDS || res.count_encoders > MAX_IDS)
		goto fail;

	// 找 connected 且带 mode 的 connector,取 modes[0](preferred/native,
	// 360/720 面板自适应)
	struct drm_mode_modeinfo modes[MAX_IDS];
	struct drm_mode_get_connector conn;
	uint32_t i;
	for (i = 0; i < res.count_connectors; i++) {
		memset(&conn, 0, sizeof(conn));
		memset(modes, 0, sizeof(modes));
		conn.connector_id = conns[i];
		conn.modes_ptr = (uintptr_t)modes;
		conn.count_modes = MAX_IDS;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0)
			continue;
		if (conn.connection == DRM_MODE_CONNECTED && conn.count_modes > 0)
			break;
	}
	if (i == res.count_connectors)
		goto fail;

	// connector 经 encoder 找 crtc(参考 drmtest.c find_crtc;本机只有
	// 一个 TCON,current crtc_id 为 0 时按 possible_crtcs 位图兜底)
	uint32_t crtc_id = 0;
	struct drm_mode_get_encoder enc = { .encoder_id = conn.encoder_id };
	if (conn.encoder_id && ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0)
		crtc_id = enc.crtc_id;
	for (i = 0; !crtc_id && i < res.count_encoders; i++) {
		memset(&enc, 0, sizeof(enc));
		enc.encoder_id = encs[i];
		if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
			continue;
		for (uint32_t j = 0; j < res.count_crtcs; j++)
			if (enc.possible_crtcs & (1u << j)) {
				crtc_id = crtcs[j];
				break;
			}
	}
	if (!crtc_id)
		goto fail;

	struct drm_mode_create_dumb creq = {
		.width = modes[0].hdisplay,
		.height = modes[0].vdisplay,
		.bpp = 32,
	};
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
		goto fail;

	// XRGB8888(depth 24 / bpp 32),同 drmtest,sun4i-drm 支持
	struct drm_mode_fb_cmd fbc = {
		.width = creq.width,
		.height = creq.height,
		.pitch = creq.pitch,
		.bpp = 32,
		.depth = 24,
		.handle = creq.handle,
	};
	if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbc) < 0)
		goto fail;

	struct drm_mode_map_dumb mreq = { .handle = creq.handle };
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0)
		goto fail;
	uint8_t *map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, mreq.offset);
	if (map == MAP_FAILED)
		goto fail;

	render(map, creq.pitch, creq.width, creq.height);

	struct drm_mode_crtc crtc = {
		.set_connectors_ptr = (uintptr_t)&conn.connector_id,
		.count_connectors = 1,
		.crtc_id = crtc_id,
		.fb_id = fbc.fb_id,
		.mode = modes[0],
		.mode_valid = 1,
	};
	if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
		goto fail;

	return fd;
fail:
	close(fd);
	return -1;
}

int main(void)
{
	for (int try = 0; try < 50; try++) {
		if (try_display() >= 0) {
			log_kmsg("sderror: SD ERROR displayed\n");
			for (;;)
				pause();
		}
		usleep(200 * 1000);
	}
	log_kmsg("sderror: giving up, display not acquired\n");
	return 1;
}

/*
 * Buildroot 包: package/epass-test,装到 /usr/bin/sderror
 *
 * 手工交叉编译 (在 buildroot 根目录,注意必须 -static 且不 -ldrm):
 *   output/host/bin/arm-buildroot-linux-musleabi-gcc --sysroot=output/staging \
 *       -O2 -Wall -static board/rhodesisland/epass/tools/sderror.c -o sderror
 */
