// sdwatch —— SD 启动模式的拔卡守护(watch + 杀进程树 + SD ERROR 显示合一)。
// 由 S02sdwatch 拷到 /tmp 运行,平时只轮询卡在不在,不打开 DRM、不申请显存;
// 拔卡(= rootfs 死)后杀掉 supervise/app 进程树,接管 DRM master 全屏红底
// "SD ERROR" 常显直到断电。
//
// 运行环境约束(拔卡后只剩 tmpfs + 内核):
//   - 必须静态链接,不碰动态 loader
//   - 不用 libdrm(.so 在死 rootfs 上),KMS 全部裸 ioctl,头文件只用内核 uapi
//   - 触发后不允许再打开 rootfs 上的任何文件;杀进程走 /proc 自己扫

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

#define DEV       "/dev/dri/card0"
#define BG        0xffcc0000u  // 红底
#define FG        0xffffffffu  // 白字
#define MAX_IDS   8            // 单 TCON 单面板,资源个数远小于此

// ---- 拔卡检测 --------------------------------------------------------------

static void log_kmsg(const char *msg)
{
	int fd = open("/dev/kmsg", O_WRONLY);
	if (fd >= 0) {
		write(fd, msg, strlen(msg));
		close(fd);
	}
}

static int sd_boot(void)
{
	char buf[512];
	int fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0)
		return 0;
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	return strstr(buf, "root=/dev/mmcblk0p2") != NULL;
}

// sysfs 只反映内核的认知:CD 脚不灵时卡拔了内核也不知道,目录一直在。
// 所以再用 O_DIRECT 直读一扇区兜底 —— 绕过 page cache 强制向卡发命令,
// 卡不在必然报错,不依赖任何中断/轮询机制。
static int card_in_sysfs(void)
{
	struct stat st;
	return stat("/sys/block/mmcblk0", &st) == 0;
}

static int card_probe(void)
{
	static uint8_t buf[512] __attribute__((aligned(512)));
	int fd = open("/dev/mmcblk0", O_RDONLY | O_DIRECT | O_CLOEXEC);
	if (fd < 0)
		return 0;
	ssize_t r = read(fd, buf, sizeof(buf));
	close(fd);
	return r == (ssize_t)sizeof(buf);
}

// ---- 杀 app 进程树 ---------------------------------------------------------

// supervise 是 sh 跑 /etc/init.d/S01app 的子 shell,comm 继承脚本名 "S01app";
// comm 上限 15 字符,"system_maintenance" 会截断,统一按前 15 字符比。
// mdev 也杀:不然后续 uevent(比如插 USB)还会按规则 exec 死 rootfs 上的
// modprobe 之类,继续刷 ext4 error
static const char *kill_names[] = {
	"S01app", "app_360", "app_720", "system_maintenance", "quick_start",
	"mdev",
};

static int kill_by_comm(int sig)
{
	DIR *d = opendir("/proc");
	if (!d)
		return 0;

	int self = getpid(), hits = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] < '1' || e->d_name[0] > '9')
			continue;
		int pid = atoi(e->d_name);
		if (pid == self)
			continue;

		char path[64], comm[32];
		snprintf(path, sizeof(path), "/proc/%d/comm", pid);
		int fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		ssize_t n = read(fd, comm, sizeof(comm) - 1);
		close(fd);
		if (n <= 0)
			continue;
		comm[n] = 0;
		if (n > 0 && comm[n - 1] == '\n')
			comm[n - 1] = 0;

		for (unsigned i = 0; i < sizeof(kill_names) / sizeof(kill_names[0]); i++)
			if (strncmp(comm, kill_names[i], 15) == 0) {
				hits++;
				if (sig)
					kill(pid, sig);
				break;
			}
	}
	closedir(d);
	return hits;
}

static int read_app_pid(void)
{
	char buf[16];
	int fd = open("/var/run/epass_app.pid", O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	return atoi(buf);
}

// ---- SD ERROR 渲染(裸 KMS,原 sderror.c) ---------------------------------

// 8x8 点阵(font8x8_basic 摘录),每字节一行,bit0 为最左像素
static const uint8_t font_S[8] = { 0x1e, 0x33, 0x07, 0x0e, 0x38, 0x33, 0x1e, 0x00 };
static const uint8_t font_D[8] = { 0x1f, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1f, 0x00 };
static const uint8_t font_E[8] = { 0x7f, 0x46, 0x16, 0x1e, 0x16, 0x46, 0x7f, 0x00 };
static const uint8_t font_R[8] = { 0x3f, 0x66, 0x66, 0x3e, 0x36, 0x66, 0x67, 0x00 };
static const uint8_t font_O[8] = { 0x1c, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x00 };
static const uint8_t font_Y[8] = { 0x33, 0x33, 0x33, 0x1e, 0x0c, 0x0c, 0x1e, 0x00 };
static const uint8_t font_T[8] = { 0x3f, 0x2d, 0x0c, 0x0c, 0x0c, 0x0c, 0x1e, 0x00 };
static const uint8_t font_M[8] = { 0x63, 0x77, 0x7f, 0x7f, 0x6b, 0x63, 0x63, 0x00 };
static const uint8_t font_H[8] = { 0x33, 0x33, 0x33, 0x3f, 0x33, 0x33, 0x33, 0x00 };
static const uint8_t font_A[8] = { 0x0c, 0x1e, 0x33, 0x33, 0x3f, 0x33, 0x33, 0x00 };
static const uint8_t font_L[8] = { 0x0f, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7f, 0x00 };
static const uint8_t font_P[8] = { 0x3f, 0x66, 0x66, 0x3e, 0x06, 0x06, 0x0f, 0x00 };
static const uint8_t font_B[8] = { 0x3f, 0x66, 0x66, 0x3e, 0x66, 0x66, 0x3f, 0x00 };
static const uint8_t font_sp[8] = { 0 };

static const uint8_t *glyph(char c)
{
	switch (c) {
	case 'S': return font_S;
	case 'D': return font_D;
	case 'E': return font_E;
	case 'R': return font_R;
	case 'O': return font_O;
	case 'Y': return font_Y;
	case 'T': return font_T;
	case 'M': return font_M;
	case 'H': return font_H;
	case 'A': return font_A;
	case 'L': return font_L;
	case 'P': return font_P;
	case 'B': return font_B;
	default:  return font_sp;
	}
}

static void draw_line(uint8_t *map, uint32_t pitch, uint32_t w, uint32_t h,
		      const char *s, uint32_t scale, uint32_t oy)
{
	int len = strlen(s);
	uint32_t tw = len * 8 * scale;
	uint32_t ox = tw < w ? (w - tw) / 2 : 0;

	for (int i = 0; i < len; i++) {
		const uint8_t *g = glyph(s[i]);
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

#define SUB_MAXLEN 22   // "PLEASE RESET THE BOARD"

static void render(uint8_t *map, uint32_t pitch, uint32_t w, uint32_t h)
{
	for (uint32_t y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(map + y * pitch);
		for (uint32_t x = 0; x < w; x++)
			row[x] = BG;
	}

	// 标题按宽度铺满 8 字符再打 8 折留边,大颗粒醒目;
	// 副标题按最长一行取能放下的最大整数倍
	uint32_t sb = w / (8 * 8) * 8 / 10;
	if (sb == 0)
		sb = 1;
	uint32_t ss = w / (SUB_MAXLEN * 8);
	if (ss == 0)
		ss = 1;

	// 竖排: 标题 | 一行空 | 副标题 | 半行空 | 副标题, 整块垂直居中
	uint32_t total = 8 * sb + 8 * ss + 8 * ss + 4 * ss + 8 * ss;
	uint32_t oy = (h > total) ? (h - total) / 2 : 0;

	draw_line(map, pitch, w, h, "SD ERROR", sb, oy);
	oy += 8 * sb + 8 * ss;
	draw_line(map, pitch, w, h, "SYSTEM HALTED", ss, oy);
	oy += 8 * ss + 4 * ss;
	draw_line(map, pitch, w, h, "PLEASE RESET THE BOARD", ss, oy);
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

static int show_sderror_forever(void)
{
	for (int try = 0; try < 100; try++) {
		if (try_display() >= 0) {
			log_kmsg("sdwatch: SD ERROR displayed\n");
			for (;;)
				pause();
		}
		usleep(200 * 1000);
	}
	log_kmsg("sdwatch: giving up, display not acquired\n");
	return 1;
}

// ---- 主流程 ----------------------------------------------------------------

int main(int argc, char **argv)
{
	// 面板单测入口:跳过守护直接渲染(正常系统先 S01app stop 再跑)
	if (argc > 1 && strcmp(argv[1], "test") == 0)
		return show_sderror_forever();

	if (!sd_boot())
		return 0;

	// 小内存 + zram 的机器,守护本体绝不能被 OOM killer 挑中,
	// 代码页也要钉在内存里(拔卡后没有任何后备文件可换入)
	int fd = open("/proc/self/oom_score_adj", O_WRONLY);
	if (fd >= 0) {
		write(fd, "-1000", 5);
		close(fd);
	}
	mlockall(MCL_CURRENT);

	// 趁 rootfs 活着缓存 supervise PID(pidfile 所在 fs 死后未必读得到)
	int app_pid = read_app_pid();

	log_kmsg("sdwatch: watching SD card\n");

	// 平时每秒看一眼 sysfs(零 IO);每 3 秒实际读一扇区兜 CD 脚失灵的场景
	int tick = 0;
	for (;;) {
		sleep(1);
		if (!card_in_sysfs())
			break;
		if (++tick >= 3) {
			tick = 0;
			if (!card_probe())
				break;
		}
	}

	log_kmsg("sdwatch: SD card removed, rootfs dead\n");

	// 先杀 supervise 防 respawn,再按 comm 扫掉 app 全家
	int pid = read_app_pid();
	if (pid <= 1)
		pid = app_pid;
	if (pid > 1)
		kill(pid, SIGTERM);
	kill_by_comm(SIGTERM);

	// 等 app 退出释放 DRM master(try_display 自带重试,这里只是加速)
	for (int i = 0; i < 30 && kill_by_comm(0) > 0; i++)
		usleep(100 * 1000);
	kill_by_comm(SIGKILL);

	return show_sderror_forever();
}

/*
 * Buildroot 包: package/epass-test,装到 /usr/sbin/sdwatch
 *
 * 手工交叉编译 (在 buildroot 根目录,注意必须 -static 且不 -ldrm):
 *   output/host/bin/arm-buildroot-linux-musleabi-gcc --sysroot=output/staging \
 *       -O2 -Wall -static board/rhodesisland/epass/tools/sdwatch.c -o sdwatch
 */
