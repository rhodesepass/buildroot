#!/usr/bin/env bash
# 强制重建自研应用包，并用 host usbaiohost(cp) 推到已连接设备。
# 用法:
#   ./rebuild-apps.sh              # 全部包
#   ./rebuild-apps.sh epassctl     # 仅指定包（可多个）
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

epcp() {
	output/host/bin/usbaiohost cp "$@"
}

ALL_PACKAGES=(
	usb_aio_handler
	epass_drm_app
	epass_applications
	epassctl
	epass-test
	battery_hwcd
)

usage() {
	echo "用法: $0 [包名...]" >&2
	echo "可用包: ${ALL_PACKAGES[*]}" >&2
	exit 1
}

is_known_package() {
	local p="$1"
	local known
	for known in "${ALL_PACKAGES[@]}"; do
		[[ "$p" == "$known" ]] && return 0
	done
	return 1
}

# 无参数 = 全部；有参数 = 仅指定包
if (($# == 0)); then
	PACKAGES=("${ALL_PACKAGES[@]}")
else
	PACKAGES=()
	for arg in "$@"; do
		if [[ "$arg" == "-h" || "$arg" == "--help" ]]; then
			usage
		fi
		if ! is_known_package "$arg"; then
			echo "错误: 未知包名 '$arg'" >&2
			usage
		fi
		PACKAGES+=("$arg")
	done
fi

package_selected() {
	local p="$1"
	local sel
	for sel in "${PACKAGES[@]}"; do
		[[ "$p" == "$sel" ]] && return 0
	done
	return 1
}

echo "==> dirclean: ${PACKAGES[*]}"
make "${PACKAGES[@]/%/-dirclean}"

echo "==> make ${PACKAGES[*]} -j16"
make -j16 "${PACKAGES[@]}"

if [[ ! -x output/host/bin/usbaiohost ]]; then
	echo "错误: 找不到 output/host/bin/usbaiohost，请确认 BR2_PACKAGE_HOST_USB_AIO_HANDLER=y" >&2
	exit 1
fi

TARGET=output/target

echo "==> 安装到设备 (usbaiohost cp)"

if package_selected usb_aio_handler; then
	# usb_aio_handler -> /usr/sbin/
	epcp \
		"$TARGET/usr/sbin/usb_aio_handler" \
		"$TARGET/usr/sbin/usbaioctl" \
		/usr/sbin/
fi

if package_selected epassctl; then
	# epassctl -> /usr/bin/
	epcp "$TARGET/usr/bin/epassctl" /usr/bin/
fi

if package_selected epass_drm_app; then
	# epass_drm_app -> /root/
	epcp "$TARGET/root/app_360" "$TARGET/root/app_720" /root/
	epcp -r "$TARGET/root/res" /root/
fi

if package_selected epass_applications; then
	# epass_applications -> /app/<name>/
	shopt -s nullglob
	app_dirs=("$TARGET"/app/*/)
	if ((${#app_dirs[@]} == 0)); then
		echo "错误: $TARGET/app/ 下没有应用目录" >&2
		exit 1
	fi
	epcp -r "${app_dirs[@]}" /app/
fi

if package_selected epass-test; then
	# epass-test -> /usr/bin/ (drmtest, c8test)
	epcp "$TARGET/usr/bin/drmtest" "$TARGET/usr/bin/c8test" /usr/bin/
fi

if package_selected battery_hwcd; then
	# battery_hwcd -> /usr/sbin/
	epcp "$TARGET/usr/sbin/battery_hwcd" /usr/sbin/
fi

echo "==> 完成"
