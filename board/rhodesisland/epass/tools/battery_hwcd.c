// epass 电量指示 daemon:读 power_supply,把状态推进内核 HWC overlay
// (/sys/kernel/battery_hwc/)。独立于主 app / DRM master,开机常驻。
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <time.h>
#include <linux/input.h>

#define HWC_DIR   "/sys/kernel/battery_hwc/"
#define PSY_DIR   "/sys/class/power_supply/"

#define COLOR_WHITE 0xffffffffu
#define COLOR_RED   0xffff0000u

#define LOW_THRESH       10      // 低于此电量红色闪烁
#define POLL_BATTERY_MS  5000
#define BLINK_MS         500     // 半周期 => 1Hz
#define SHOW_MS          3000    // 常态按键后显示时长

#define MAX_INPUTS 16

static char psy_path[256];
static int  cap_reliable = 1;    // 充电时 capacity 是否可信;文件不存在=可信(AXP)

static struct pollfd inputs[MAX_INPUTS];
static int input_n;

static int read_file(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	int r = read(fd, buf, n - 1);
	close(fd);
	if (r < 0)
		return -1;
	buf[r] = 0;
	return r;
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int find_battery(void)
{
	glob_t g;
	int found = -1;

	if (glob(PSY_DIR "*", 0, NULL, &g))
		return -1;
	for (size_t i = 0; i < g.gl_pathc; i++) {
		char p[300], buf[32];

		snprintf(p, sizeof p, "%s/type", g.gl_pathv[i]);
		if (read_file(p, buf, sizeof buf) < 0)
			continue;
		if (!strncmp(buf, "Battery", 7)) {
			snprintf(psy_path, sizeof psy_path, "%s", g.gl_pathv[i]);
			found = 0;
			break;
		}
	}
	globfree(&g);
	return found;
}

static int psy_int(const char *attr, int dflt)
{
	char p[300], buf[32];

	snprintf(p, sizeof p, "%s/%s", psy_path, attr);
	if (read_file(p, buf, sizeof buf) < 0)
		return dflt;
	return atoi(buf);
}

static int psy_charging(void)
{
	char p[300], buf[32];

	snprintf(p, sizeof p, "%s/status", psy_path);
	if (read_file(p, buf, sizeof buf) < 0)
		return 0;
	return !strncmp(buf, "Charging", 8);
}

// epass_drm_app(app_360/app_720)自带电量显示;它在跑时 daemon 让位关掉 overlay
static int app_running(void)
{
	glob_t g;
	int found = 0;

	if (glob("/proc/[0-9]*/comm", 0, NULL, &g))
		return 0;
	for (size_t i = 0; i < g.gl_pathc && !found; i++) {
		char buf[64];

		if (read_file(g.gl_pathv[i], buf, sizeof buf) < 0)
			continue;
		if (!strcmp(buf, "app_360\n") || !strcmp(buf, "app_720\n"))
			found = 1;
	}
	globfree(&g);
	return found;
}

// cardkb 是模块,开机时可能还没注册;input_n==0 时重扫一次即可补上
static void open_inputs(void)
{
	glob_t g;

	for (int i = 0; i < input_n; i++)
		close(inputs[i].fd);
	input_n = 0;
	if (glob("/dev/input/event*", 0, NULL, &g) == 0) {
		for (size_t i = 0; i < g.gl_pathc && input_n < MAX_INPUTS; i++) {
			int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);

			if (fd < 0)
				continue;
			inputs[input_n].fd = fd;
			inputs[input_n].events = POLLIN;
			input_n++;
		}
		globfree(&g);
	}
}

static int drain_any_key(int idx)
{
	struct input_event ev;
	int pressed = 0;

	while (read(inputs[idx].fd, &ev, sizeof ev) == (int)sizeof ev)
		if (ev.type == EV_KEY && ev.value == 1)
			pressed = 1;
	return pressed;
}

struct hwc {
	int enable, percent, show_percent, charging;
	unsigned color;
};

static void wr(const char *attr, const char *fmt, unsigned v)
{
	char path[300], val[32];
	int len = snprintf(val, sizeof val, fmt, v);

	snprintf(path, sizeof path, HWC_DIR "%s", attr);
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return;                 // 内核驱动没起来时静默重试
	ssize_t n = write(fd, val, len);
	(void)n;
	close(fd);
}

// 只写变化的属性:每次写都会触发内核重上传 pattern + commit
static void hwc_push(const struct hwc *w)
{
	static struct hwc cur = { -1, -1, -1, -1, 0xdeadbeef };

	// 参数先写,enable 最后,保证使能时 pattern 已是新的
	if (w->color != cur.color)               { wr("color", "0x%08x\n", w->color); cur.color = w->color; }
	if (w->percent != cur.percent)           { wr("percent", "%u\n", w->percent); cur.percent = w->percent; }
	if (w->show_percent != cur.show_percent) { wr("show_percent", "%u\n", w->show_percent); cur.show_percent = w->show_percent; }
	if (w->charging != cur.charging)         { wr("charging", "%u\n", w->charging); cur.charging = w->charging; }
	if (w->enable != cur.enable)             { wr("enable", "%u\n", w->enable); cur.enable = w->enable; }
}

int main(void)
{
	while (find_battery() != 0)
		sleep(2);
	cap_reliable = psy_int("charging_capacity_reliable", 1) != 0;
	open_inputs();

	int capacity = psy_int("capacity", 100);
	int charging = psy_charging();
	int display_capacity = capacity;   // 显示用电量:未充电时只降不升
	int app_up = app_running();
	long next_batt = now_ms();
	long show_until = 0;
	long next_blink = 0;
	int blink_on = 0;

	for (;;) {
		long tnow = now_ms();

		if (tnow >= next_batt) {
			capacity = psy_int("capacity", capacity);
			charging = psy_charging();
			// GAB 后端(cap_reliable==0)是 OCV 查表连续值,抖动大;
			// 量化到四格(0/25/50/75/100),就近取整。AXP 是真库仑计,不动
			if (!cap_reliable) {
				capacity = (capacity + 12) / 25 * 25;
				if (capacity > 100)
					capacity = 100;
			}
			// 充电前假定电量只会往下掉:取最小值平滑后端(GAB/AXP)读数抖动;
			// 充电时跟随实际值(会上升),充电结束后以当前值为新的下降基准
			if (charging)
				display_capacity = capacity;
			else if (capacity < display_capacity)
				display_capacity = capacity;
			app_up = app_running();
			if (input_n == 0)
				open_inputs();
			next_batt = tnow + POLL_BATTERY_MS;
		}

		struct hwc w = { 0, display_capacity, 1, 0, COLOR_WHITE };
		long timeout = next_batt - tnow;

		if (charging) {
			w.charging = 1;
			w.show_percent = cap_reliable ? 1 : 0;
			w.enable = 1;
		} else if (display_capacity < LOW_THRESH) {
			w.color = COLOR_RED;
			if (tnow >= next_blink) {
				blink_on = !blink_on;
				next_blink = tnow + BLINK_MS;
			}
			w.enable = blink_on;
			long tb = next_blink - tnow;
			if (tb < timeout)
				timeout = tb;
		} else if (show_until && tnow < show_until) {
			w.enable = 1;
			long ts = show_until - tnow;
			if (ts < timeout)
				timeout = ts;
		} else {
			show_until = 0;
		}

		if (app_up)
			w.enable = 0;   // app 自带电量显示,daemon 让位

		hwc_push(&w);

		if (timeout < 0)
			timeout = 0;
		if (timeout > POLL_BATTERY_MS)
			timeout = POLL_BATTERY_MS;
		if (poll(inputs, input_n, (int)timeout) > 0) {
			int key = 0;

			for (int i = 0; i < input_n; i++)
				if ((inputs[i].revents & POLLIN) && drain_any_key(i))
					key = 1;
			if (key && !charging && display_capacity >= LOW_THRESH)
				show_until = now_ms() + SHOW_MS;
		}
	}
	return 0;
}
