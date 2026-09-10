#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装 macOS 发布包 —— 结构镜像 Windows 免安装工具箱（docs/windows-toolbox.md，
scripts/make_toolbox.py 组装），但 macOS 上不内置 cmake/ninja/编译器（Xcode 命令行
工具 + 一个装好的 CMake 就够，多数开发机上已经有）。

    python3 scripts/make_bundle_mac.py

产出 build/pack/Easel-<版本>-macos/（以及同名 .zip）：

    Easel-0.1.1-macos/
    ├── Easel.app/                本机编的（EASEL_MACOS_BUNDLE=ON + Release），已 ad-hoc 签名
    │   └── Contents/
    │       ├── MacOS/Easel       可执行文件
    │       ├── easel             → 软链到 MacOS/Easel（命令行入口，小写好敲）
    │       ├── Resources/Easel.icns
    │       ├── Resources/easel/  源码树 + prebuilt/（cmake --install 的 prefix）+ VERSION.json
    │       └── Info.plist
    ├── README.txt
    ├── LICENSE.txt
    └── licenses/                 vendor/*/LICENSE* 收的三方许可证

包根不再放 easel/ 源码树——整个都收进 Easel.app 内部了（一个 60MB 的目录跟 .app
并排，看起来不像个正常的 Mac 应用；D-26 的绿色工具箱布局本来是照顾 Windows 那种
「解压即用、没有安装步骤」的心智模型，macOS 上直接用 .app 自己的容器就够）。

命令行软链放在 Contents/ 这一级，**不是** Contents/MacOS/（跟 Easel 可执行文件同级）——
实测过：Mac 默认的文件系统（APFS/HFS+）大小写不敏感但大小写保留，同一目录里 "easel"
和 "Easel" 是同一个目录项，在 Contents/MacOS/ 下建 easel -> Easel 这条软链会直接把
可执行文件 Easel 顶掉，变成一个自己指向自己的死链接（双击/命令行会报「Too many
levels of symbolic links」）。Contents/ 这一级没有任何叫 Easel 的东西，不会撞。

脚本自己负责：配置 + Release 构建 easel_workbench、把源码树 + cmake --install 的
prefix + VERSION.json 全拷进 Contents/Resources/easel/、收许可证、建
Contents/easel 软链、（都弄完之后才）ad-hoc 签名、（默认）打 zip。

**要在 macOS 上跑**（Easel.app 是本机编译器编的，不可能在别的平台上产出）。
**签名顺序要紧**：codesign 必须放在所有内容都拷进 Contents/ 之后——ad-hoc 签名是
对整个 bundle（包括 Resources/ 底下新增的 easel/ 源码树、prebuilt/）签的，签完后
再改动 Contents/ 下任何文件都会让签名失效。
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone

# scripts/abi.py 就在这个文件旁边——ABI 指纹算法只有这一份实现，根 CMakeLists.txt
# 和 scripts/make_toolbox.py 用的也是它。这里其实用不上直接调用它：cmake --install
# 已经把 abi 算好写进了 easel/prebuilt/easel-prebuilt.json，直接抄过来即可（本机
# clang 刚编的这份和源码树是同一次构建，天然一致，没必要再算一遍）。

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

if sys.platform != "darwin":
    print("[发布包] 这个脚本只能在 macOS 上跑（要用系统自带的 sips/iconutil/ditto/codesign）",
          file=sys.stderr)
    sys.exit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 打进 <Easel.app>/Contents/Resources/easel/ 的东西。比 Windows 工具箱那份少了
# CMakePresets.json / scripts / workbench / tests / README.md —— 学生工程走的是
# --new 生成的 .easel/CMakeLists.txt（直接 -DEASEL_DIR=<路径>），不需要 Easel 自己的
# preset 或工作台源码；但 dist/（单头库 easel.hpp 摊平版）必须带上，
# src/new_project.cpp 生成的 CMakeLists 里 EASEL_CORE_DIR = ${EASEL_DIR}/dist，
# solver.cpp #include "easel.hpp" 就是从这里找。
EASEL_ITEMS = ["CMakeLists.txt", "LICENSE", "cmake", "include", "src", "vendor", "assets",
               "dist", "template", "template-hello", "examples", "docs"]

README_TXT = """Easel {version} · macOS 发布包

把 Easel.app 拖进「应用程序」（/Applications）即可，双击打开工作台（第一次可能
要右键 -> 打开，绕开「无法验证开发者」提示）。

第一次用之前，先在终端里装好 Xcode 命令行工具：
    xcode-select --install
{cmake_note}
新建的工程默认放在 ~/projects（工程名只能用英文字母/数字/下划线）。

打不开、或者画面不对，在终端里跑（把输出发给维护者）：
    /Applications/Easel.app/Contents/MacOS/Easel --doctor

命令行用法（Contents/ 下有个 easel 软链，指向 Contents/MacOS/Easel，两种写法都行）：
    /Applications/Easel.app/Contents/easel --build ~/projects/Demo

想更好敲，可以自己软链到 /usr/local/bin：
    ln -s /Applications/Easel.app/Contents/easel /usr/local/bin/easel
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
    """从 include/easel/core.h 的 #define EASEL_VERSION "x.y.z" 里取版本号，读不到就 0.1.1。"""
    header = os.path.join(ROOT, "include", "easel", "core.h")
    try:
        text = open(header, encoding="utf-8").read()
        m = re.search(r'#define\s+EASEL_VERSION\s+"([0-9.]+)"', text)
        if m:
            return m.group(1)
    except OSError:
        pass
    return "0.1.1"


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


def write_version_json(dest, commit, abi):
    payload = {
        "version": easel_version(),
        "commit": commit or "unknown",
        "abi": abi or "unknown",
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
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "pack"))
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

    shutil.rmtree(out_root, ignore_errors=True)
    os.makedirs(out_root)

    print("=== 3/6 装 Easel.app（ditto，保住符号链接/权限）===")
    app_dest = os.path.join(out_root, "Easel.app")
    run(["ditto", app_src, app_dest])
    # 先不签名——Resources/easel/（源码树 + prebuilt/）还没拷进去，签早了后面一改
    # Contents/ 下的内容签名就失效了。签名放在第 5 步，所有内容拷完之后。

    print("=== 4/6 源码树 + prebuilt 装进 Contents/Resources/easel/ ===")
    resources_easel = os.path.join(app_dest, "Contents", "Resources", "easel")
    # 顺序要紧：copy_easel_tree() 会先 rmtree 整个 easel/ 再重建，所以必须先拷源码树，
    # 再 cmake --install 到 easel/prebuilt——反过来的话装好的预编译包会被这一步删掉
    # （曾经真的这么写过：装完预编译包，下一步把 easel/ 整个删了重建，prebuilt/ 一起没了，
    # 学生工程找不到 easelConfig.cmake，只能退回源码编，三分钟而不是几秒钟）。
    missing = copy_easel_tree(resources_easel)
    if missing:
        print(f"  （跳过不存在的：{', '.join(missing)}）")

    prebuilt_dir = os.path.join(resources_easel, "prebuilt")
    run(["cmake", "--install", build_dir, "--prefix", prebuilt_dir])
    print(f"    {dir_size(prebuilt_dir)/1e6:.0f} MB")

    commit = git_commit(ROOT)
    # 直接抄 cmake --install 刚写的 easel-prebuilt.json 里的 abi——本机 clang 刚编的
    # 这份和源码树是同一次构建产的，天然一致，不用再调 abi.py 重算一遍。
    abi = "unknown"
    prebuilt_stamp = os.path.join(prebuilt_dir, "easel-prebuilt.json")
    if os.path.exists(prebuilt_stamp):
        with open(prebuilt_stamp, encoding="utf-8") as f:
            abi = json.load(f).get("abi", "unknown")
    write_version_json(os.path.join(resources_easel, "VERSION.json"), commit, abi)
    print(f"  源码戳 → Resources/easel/VERSION.json（commit {commit or 'unknown'}，abi {abi}）")

    print("=== 5/6 命令行软链 + ad-hoc 签名（要放在所有内容拷完之后，否则签名失效）===")
    # 命令行友好的软链：easel -> MacOS/Easel。放在 Contents/ 这一级而不是 Contents/MacOS/
    # 下——Mac 文件系统大小写不敏感但保留大小写，同一目录里 "easel" 和 "Easel" 是同一个
    # 目录项，在 Contents/MacOS/ 下建 easel -> Easel 会直接把可执行文件 Easel 顶掉，变成
    # 自己指向自己的死链（报「Too many levels of symbolic links」）。Contents/ 下没有任何
    # 叫 Easel 或 MacOS 的东西，不会撞（MacOS 是目录，不是文件，不算目录项）。
    easel_link = os.path.join(app_dest, "Contents", "easel")
    if os.path.islink(easel_link) or os.path.exists(easel_link):
        os.remove(easel_link)
    os.symlink("MacOS/Easel", easel_link)
    # Apple Silicon 上没签名连右键「打开」都不行；ad-hoc（-s -）不需要开发者证书。
    # --deep 会把 bundle 里任何嵌套的可执行文件/bundle 也一起签一遍；这一步必须在
    # 上面所有拷贝/写文件动作之后，否则签完名再改 Contents/ 下的内容，Gatekeeper
    # 校验时会发现内容跟签名不一致（对得上 CodeResources 里的哈希才算数）。
    run(["codesign", "--force", "--deep", "-s", "-", app_dest])

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
