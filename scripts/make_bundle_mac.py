#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装 macOS 发布包 —— 结构镜像 windows-green 的免安装工具箱，但 macOS 上不内置
cmake/ninja/编译器（Xcode 命令行工具 + 一个装好的 CMake 就够，多数开发机上已经有）。

    python3 scripts/make_bundle_mac.py

产出 dist-mac/Easel-<版本>-macos/（以及同名 .zip）：

    Easel-0.1.1-macos/
    ├── Easel.app                 本机编的（EASEL_MACOS_BUNDLE=ON + Release），已 ad-hoc 签名
    ├── README.txt
    ├── LICENSE.txt
    ├── licenses/                 vendor/*/LICENSE* 收的三方许可证
    └── easel/
        ├── prebuilt/             cmake --install 的 prefix（本机 clang 编的）
        ├── VERSION.json
        ├── CMakeLists.txt cmake/ include/ src/ vendor/ assets/ LICENSE
        ├── dist/                 摊平的单头库 easel.hpp（solver.cpp / --new 生成的工程都要它）
        └── template/ template-hello/ examples/ docs/

脚本自己负责：配置 + Release 构建 easel_workbench、cmake --install 到 easel/prebuilt、
拷源码树、写 VERSION.json、收许可证、ad-hoc 签名、（默认）打 zip。

**要在 macOS 上跑**（Easel.app 是本机编译器编的，不可能在别的平台上产出）。
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

if sys.platform != "darwin":
    print("[发布包] 这个脚本只能在 macOS 上跑（要用系统自带的 sips/iconutil/ditto/codesign）",
          file=sys.stderr)
    sys.exit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 打进 <包>/easel/ 的东西。比 windows-green 那份少了 CMakePresets.json / scripts / workbench /
# tests / README.md —— 学生工程走的是 --new 生成的 .easel/CMakeLists.txt（直接
# -DEASEL_DIR=<路径>），不需要 Easel 自己的 preset 或工作台源码；但 dist/（单头库
# easel.hpp 摊平版）必须带上，src/new_project.cpp 生成的 CMakeLists 里
# EASEL_CORE_DIR = ${EASEL_DIR}/dist，solver.cpp #include "easel.hpp" 就是从这里找。
EASEL_ITEMS = ["CMakeLists.txt", "LICENSE", "cmake", "include", "src", "vendor", "assets",
               "dist", "template", "template-hello", "examples", "docs"]

README_TXT = """Easel {version} · macOS 发布包

双击 Easel.app 打开工作台（第一次可能要右键 -> 打开，绕开「无法验证开发者」提示）。

第一次用之前，先在终端里装好 Xcode 命令行工具：
    xcode-select --install
{cmake_note}
新建的工程默认放在 ~/projects（工程名只能用英文字母/数字/下划线）。

打不开、或者画面不对，在终端里跑（把输出发给维护者）：
    Easel.app/Contents/MacOS/Easel --doctor
"""

# 不内置 cmake/ninja（Windows 版工具箱带，这份不带）——不管打包这台机器上有没有
# cmake，用户自己的 Mac 上编工程都需要装一个，所以这句提示始终写进 README。
CMAKE_NOTE = """
这份包里不带 CMake（Windows 版工具箱带，macOS 上没有内置）——编译工程前
需要先装一个：`brew install cmake`，或去 https://cmake.org/download/ 下官方 dmg。
"""


def run(cmd, cwd=None):
    print("   ", " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        raise SystemExit(f"[发布包] 这条命令失败了：{' '.join(str(c) for c in cmd)}")


def dir_size(path):
    return sum(os.path.getsize(os.path.join(dp, f))
               for dp, _, fs in os.walk(path) for f in fs)


def git_commit(repo):
    try:
        out = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=repo,
                              capture_output=True, text=True, check=True)
        return out.stdout.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def easel_version():
    cmake = os.path.join(ROOT, "CMakeLists.txt")
    text = open(cmake, encoding="utf-8").read()
    m = re.search(r"project\(\s*easel\s+VERSION\s+([0-9.]+)", text)
    return m.group(1) if m else "0.0.0"


def copy_easel_tree(dest):
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    missing = []
    for item in EASEL_ITEMS:
        src = os.path.join(ROOT, item)
        if not os.path.exists(src):
            missing.append(item)
            continue
        dst = os.path.join(dest, item)
        if os.path.isdir(src):
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns(
                "build", "__pycache__", "*.pyc", ".DS_Store", ".git"))
        else:
            shutil.copy2(src, dst)
    return missing


def write_version_json(dest, commit):
    payload = {
        "version": easel_version(),
        "commit": commit or "unknown",
        "compiler": "",
        "system": "",
        "date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.write("\n")


def collect_licenses(dest):
    """把 Easel 自己的 LICENSE 和 vendor/*/LICENSE* 收进 dest（跟 package.py 的
    collect_licenses 是同一套办法）。"""
    os.makedirs(dest, exist_ok=True)
    n = 0
    lic = os.path.join(ROOT, "LICENSE")
    if os.path.exists(lic):
        shutil.copy2(lic, os.path.join(dest, "Easel-LICENSE.txt"))
        n += 1
    vendor = os.path.join(ROOT, "vendor")
    if os.path.isdir(vendor):
        for dep in sorted(os.listdir(vendor)):
            d = os.path.join(vendor, dep)
            if not os.path.isdir(d):
                continue
            for cand in ("LICENSE", "LICENSE.txt", "LICENSE.md", "LICENSE.TXT",
                         "LICENSE.MIT", "COPYING", "LICENSE-MIT", "license.txt"):
                f = os.path.join(d, cand)
                if os.path.exists(f):
                    shutil.copy2(f, os.path.join(dest, f"{dep}-LICENSE.txt"))
                    n += 1
                    break
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist-mac"))
    ap.add_argument("--no-zip", action="store_true")
    args = ap.parse_args()

    if not shutil.which("cmake"):
        print("[发布包] 这台机器上没有 cmake，打不了包（打包本身要用它配置/编译/安装 Easel）",
              file=sys.stderr)
        return 2

    if not os.path.isdir(os.path.join(ROOT, "vendor")):
        print("[发布包] 没有 vendor/ —— 先跑 python3 scripts/vendor.py", file=sys.stderr)
        return 2

    version = easel_version()
    name = f"Easel-{version}-macos"
    out_root = os.path.join(os.path.abspath(args.out), name)
    build_dir = os.path.join(ROOT, "build", "mac-release")

    print(f"=== 1/6 配置（EASEL_MACOS_BUNDLE=ON, Release）===")
    run(["cmake", "-S", ROOT, "-B", build_dir,
         "-DCMAKE_BUILD_TYPE=Release", "-DEASEL_MACOS_BUNDLE=ON",
         "-DEASEL_BUILD_EXAMPLES=OFF", "-DEASEL_BUILD_TESTS=OFF"])

    print("=== 2/6 编译 easel_workbench（Easel.app）===")
    run(["cmake", "--build", build_dir, "--target", "easel_workbench", "--parallel"])

    app_src = os.path.join(build_dir, "Easel.app")
    if not os.path.isdir(app_src):
        print(f"[发布包] 编完没找到 {app_src}——EASEL_MACOS_BUNDLE 没生效？", file=sys.stderr)
        return 2

    if os.path.isdir(out_root):
        shutil.rmtree(out_root)
    os.makedirs(out_root)

    print("=== 3/6 装 Easel.app（ditto，保住符号链接/权限）+ ad-hoc 签名 ===")
    app_dest = os.path.join(out_root, "Easel.app")
    run(["ditto", app_src, app_dest])
    # Apple Silicon 上没签名连右键「打开」都不行；ad-hoc（-s -）不需要开发者证书。
    run(["codesign", "--force", "--deep", "-s", "-", app_dest])

    print("=== 4/6 源码树 ===")
    # 顺序要紧：copy_easel_tree() 会先 rmtree 整个 easel/ 再重建，所以必须先拷源码树，
    # 再 cmake --install 到 easel/prebuilt——反过来的话装好的预编译包会被这一步删掉
    # （曾经真的这么写过：装完预编译包，下一步把 easel/ 整个删了重建，prebuilt/ 一起没了，
    # 学生工程找不到 easelConfig.cmake，只能退回源码编，三分钟而不是几秒钟）。
    missing = copy_easel_tree(os.path.join(out_root, "easel"))
    if missing:
        print(f"  （跳过不存在的：{', '.join(missing)}）")

    print("=== 5/6 cmake --install 到 easel/prebuilt + VERSION.json + licenses ===")
    prebuilt_dir = os.path.join(out_root, "easel", "prebuilt")
    run(["cmake", "--install", build_dir, "--prefix", prebuilt_dir])
    print(f"    {dir_size(prebuilt_dir)/1e6:.0f} MB")

    commit = git_commit(ROOT)
    write_version_json(os.path.join(out_root, "easel", "VERSION.json"), commit)
    print(f"  源码戳 → easel/VERSION.json（commit {commit or 'unknown'}）")

    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(out_root, "LICENSE.txt"))
    nlic = collect_licenses(os.path.join(out_root, "licenses"))
    print(f"  licenses/ 收了 {nlic} 份")

    with open(os.path.join(out_root, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README_TXT.format(version=version, cmake_note=CMAKE_NOTE))

    print(f"    展开后 {dir_size(out_root)/1e6:.0f} MB")

    if args.no_zip:
        print(f"好了：{out_root}")
        return 0

    print("=== 6/6 打包（ditto -c -k）===")
    zip_path = os.path.join(os.path.abspath(args.out), f"{name}.zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", out_root, zip_path])
    print(f"好了：{zip_path}（{os.path.getsize(zip_path)/1e6:.0f} MB）")
    print("解压后，双击 Easel.app（Apple Silicon 上第一次可能要右键 -> 打开）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
