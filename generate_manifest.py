#!/usr/bin/env python3
"""生成当前 buildroot output 的 schema-3 固件 manifest。

schema 与角色定义以 ePass 设备管理器的 FlashManifest 解析器为准：
schema == 3，每个烧录目标（nand/sd）都必须提供 felboot/uboot/boot/rootfs 四个角色。
文件到角色的映射见下方 TARGETS，与 buildroot/flash.py 的烧录流程一致。

用法示例：
    ./generate_manifest.py                       # 用 git 推导版本，写到 ./manifest-v3.json
    ./generate_manifest.py -o -                  # 输出到 stdout
    ./generate_manifest.py --version 3.0.0 --title "3.0 正式版" \\
        --description "3.0 系统首个正式版。"
"""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys

SCHEMA = 3

# role -> 文件名。felboot 是 FEL 阶段 xfel 载入的 u-boot，两个目标共用。
TARGETS = {
    "nand": {
        "felboot": "u-boot.bin",
        "uboot": "u-boot-sunxi-with-nand-spl.bin",
        "boot": "boot.itb",
        "rootfs": "rootfs_ubi.img",
    },
    "sd": {
        "felboot": "u-boot.bin",
        "uboot": "u-boot-sunxi-with-spl.bin",
        "boot": "bootfs.vfat",
        "rootfs": "rootfs.ext4",
    },
}

DEFAULT_MIRRORS = [
    {
        "name": "星语Studio分发（感谢！！！）",
        "url": "https://openlist.slstudio.top/sd/Siv0HleO/${version}/${file}"
    },
    {
        "name": "github",
        "url": "https://github.com/rhodesepass/buildroot/releases/download/${version}/${file}"
    },
    {
        "name": "gh-proxy",
        "url": "https://gh-proxy.com/https://github.com/rhodesepass/buildroot/releases/download/${version}/${file}"
    },
    {
        "name": "白银oss",
        "url": "https://shirogane.oss-cn-shanghai.aliyuncs.com/epass/${version}/${file}"
    }
]


def git_output(repo_dir, *args):
    try:
        out = subprocess.run(
            ["git", "-C", repo_dir, *args],
            capture_output=True,
            text=True,
            check=True,
        )
        return out.stdout.strip()
    except Exception:
        return ""


def sha256_and_size(path):
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            h.update(chunk)
    return h.hexdigest(), size


def parse_mirrors(pairs):
    mirrors = []
    for pair in pairs:
        if "=" not in pair:
            raise SystemExit(f"--mirror 需要 名称=URL 形式，收到: {pair!r}")
        name, url = pair.split("=", 1)
        mirrors.append({"name": name.strip(), "url": url.strip()})
    return mirrors


def build_entry(images_dir, args):
    # 逐个目标检查文件是否齐全；整组齐全才纳入，缺文件的目标跳过并告警，
    # 避免生成一份“看起来完整实则缺件”的危险 manifest。
    emitted_targets = {}
    referenced = {}  # name -> 绝对路径（用于算 hash，rootfs.ext4 可能是软链）
    for target, roles in TARGETS.items():
        resolved = {}
        missing = []
        for role, name in roles.items():
            path = os.path.join(images_dir, name)
            if os.path.exists(path):
                resolved[name] = path
            else:
                missing.append(name)
        if missing:
            print(
                f"[skip] 目标 {target} 缺少文件: {', '.join(missing)}",
                file=sys.stderr,
            )
            continue
        emitted_targets[target] = dict(roles)
        referenced.update(resolved)

    if not emitted_targets:
        raise SystemExit(f"{images_dir} 下没有任何完整的烧录目标，未生成 manifest。")

    files = []
    for name in sorted(referenced):
        digest, size = sha256_and_size(referenced[name])
        files.append({"name": name, "sha256": digest, "size": size})
        print(f"[ok]   {name}  {size} bytes  {digest}", file=sys.stderr)

    return {
        "version": args.version,
        "channel": args.channel,
        "title": args.title,
        "commit": args.commit,
        "description": args.description,
        "targets": emitted_targets,
        "files": files,
    }


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(
        description="生成当前 buildroot output 的 schema-3 固件 manifest。"
    )
    parser.add_argument(
        "--images-dir",
        default=os.path.join(script_dir, "output", "images"),
        help="镜像目录，默认 output/images",
    )
    parser.add_argument("--version", default=None, help="固件版本号，默认由 git describe 推导")
    parser.add_argument("--commit", default=None, help="commit 短哈希，默认取当前 HEAD")
    parser.add_argument("--channel", default="stable", help="发布通道，默认 stable")
    parser.add_argument("--title", default=None, help="显示标题，默认同 version")
    parser.add_argument("--description", default="", help="更新说明")
    parser.add_argument(
        "--mirror",
        action="append",
        default=[],
        metavar="名称=URL",
        help="下载镜像，可重复。URL 支持 ${version}/${file} 占位。不指定则用默认主站。",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=os.path.join(script_dir, "manifest-v3.json"),
        help="输出文件路径，'-' 表示 stdout，默认 ./manifest-v3.json",
    )
    args = parser.parse_args()

    if args.version is None:
        args.version = git_output(script_dir, "describe", "--tags", "--always", "--dirty") or "0.0.0"
    if args.commit is None:
        args.commit = git_output(script_dir, "rev-parse", "--short", "HEAD")
    if args.title is None:
        args.title = args.version

    images_dir = args.images_dir
    if not os.path.isdir(images_dir):
        raise SystemExit(f"镜像目录不存在: {images_dir}")

    entry = build_entry(images_dir, args)

    manifest = {
        "schema": SCHEMA,
        "generated_at": datetime.datetime.now(datetime.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "available_mirror": parse_mirrors(args.mirror) if args.mirror else DEFAULT_MIRRORS,
        "manifest": [entry],
    }

    text = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
    if args.output == "-":
        sys.stdout.write(text)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"\n已写入 {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
