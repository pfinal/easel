#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 CMakeLists.txt 里声明的九个依赖抓到 vendor/，之后构建完全不用联网。

给机房、还原卡、出不去外网的机器用。抓完之后：

    cmake --preset default        # CMake 自己会发现 vendor/，一个参数都不用加

版本号不用在这里重复写 —— 直接从 CMakeLists.txt 的 FetchContent_Declare 里读，
所以永远不会和构建脚本对不上。

用法：
    python3 scripts/vendor.py              # 抓到 ./vendor/
    python3 scripts/vendor.py --out D:/kit/vendor
    python3 scripts/vendor.py --check      # 只检查有没有、版本对不对
"""
import argparse
import io
import json
import os
import re
import shutil
import sys
import tarfile
import urllib.request

# Windows 上标准输出默认 cp1252，打中文会炸；统一成 UTF-8（Python 3.7+）
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKELISTS = os.path.join(ROOT, "CMakeLists.txt")

DECLARE = re.compile(
    r"FetchContent_Declare\(\s*(\w+)\s+GIT_REPOSITORY\s+(\S+?)\s+GIT_TAG\s+(\S+)", re.S)


def deps():
    """从 CMakeLists.txt 读出 (名字, owner/repo, ref) 三元组。"""
    text = open(CMAKELISTS, encoding="utf-8").read()
    out = []
    for name, url, tag in DECLARE.findall(text):
        m = re.search(r"github\.com/([^/]+/[^/.\s]+)", url)
        if not m:
            print(f"[vendor] 不认识的仓库地址，跳过：{url}", file=sys.stderr)
            continue
        out.append((name, m.group(1), tag.strip().rstrip(')').strip()))
    return out


def fetch(repo, ref, dest):
    """下 tar.gz 解开到 dest（不需要 git）。"""
    url = f"https://codeload.github.com/{repo}/tar.gz/{ref}"
    print(f"[vendor] 下载 {repo}@{ref}")
    with urllib.request.urlopen(url, timeout=180) as r:
        blob = r.read()
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tf:
        members = tf.getmembers()
        top = members[0].name.split("/")[0] + "/"
        for m in members:
            if not m.name.startswith(top):
                continue
            m.name = m.name[len(top):]
            if not m.name:
                continue
            # 防目录穿越
            target = os.path.realpath(os.path.join(dest, m.name))
            if not target.startswith(os.path.realpath(dest)):
                raise RuntimeError(f"压缩包里有可疑路径：{m.name}")
            tf.extract(m, dest)
    return len(blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "vendor"))
    ap.add_argument("--check", action="store_true", help="只检查，不下载")
    args = ap.parse_args()

    wanted = deps()
    if len(wanted) != 9:
        print(f"[vendor] 从 CMakeLists.txt 只读出 {len(wanted)} 个依赖，预期 9 个", file=sys.stderr)

    stamp_path = os.path.join(args.out, "versions.json")
    have = {}
    if os.path.exists(stamp_path):
        have = json.load(open(stamp_path, encoding="utf-8"))

    if args.check:
        bad = []
        for name, repo, ref in wanted:
            if not os.path.isdir(os.path.join(args.out, name)):
                bad.append(f"{name}：没有")
            elif have.get(name, {}).get("ref") != ref:
                bad.append(f"{name}：是 {have.get(name, {}).get('ref')}，应该是 {ref}")
        if bad:
            print("[vendor] vendor/ 不完整或版本对不上：")
            for b in bad:
                print("   ", b)
            print("    跑一下 python3 scripts/vendor.py")
            return 1
        print(f"[vendor] {args.out} 里 {len(wanted)} 个依赖齐全，版本正确")
        return 0

    os.makedirs(args.out, exist_ok=True)
    total = 0
    stamp = {}
    for name, repo, ref in wanted:
        dest = os.path.join(args.out, name)
        if have.get(name, {}).get("ref") == ref and os.path.isdir(dest):
            print(f"[vendor] {name}@{ref} 已经有了，跳过")
            stamp[name] = have[name]
            continue
        size = fetch(repo, ref, dest)
        total += size
        stamp[name] = {"repo": repo, "ref": ref}

    json.dump(stamp, open(stamp_path, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    with open(os.path.join(args.out, "README.md"), "w", encoding="utf-8") as f:
        f.write("# vendor —— 依赖源码（离线构建用）\n\n")
        f.write("这个目录是 `scripts/vendor.py` 生成的，**不要手改，也不要提交进 git**。\n\n")
        f.write("只要它存在，`cmake` 就不会再联网下载依赖，什么参数都不用加。\n\n")
        f.write("| 依赖 | 仓库 | 版本 |\n|---|---|---|\n")
        for name, repo, ref in wanted:
            f.write(f"| {name} | {repo} | {ref} |\n")
        f.write("\n各依赖的许可证在各自目录里，分发时请一并带上。\n")

    on_disk = sum(os.path.getsize(os.path.join(dp, fn))
                  for dp, _, fns in os.walk(args.out) for fn in fns)
    print(f"[vendor] 好了：{args.out}，下载 {total/1e6:.1f} MB，展开后 {on_disk/1e6:.1f} MB")
    print("[vendor] 现在 cmake --preset default 会完全离线构建")
    return 0


if __name__ == "__main__":
    sys.exit(main())
