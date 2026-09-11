#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装 Linux 发布包（tar.gz）—— 跟 macOS 那份（scripts/make_bundle_mac.py）同一个
思路：可执行文件 + Easel 源码 + cmake --install 的预编译包 + vendor 依赖源码 +
模板 / 示例 / 文档 + 许可证，解压即用；编译学生工程仍然靠系统的 g++ / cmake（不
自带工具链，跟 macOS 版一样）。

跟 macOS 那份最大的不同：这个脚本**不自己编译**——它只组装，编译/安装交给调用者
先做好（跟 scripts/make_toolbox.py 认 --prebuilt-dir 是同一个道理）。原因：
Linux 发布包必须在 ubuntu-22.04 上编（链到旧版 glibc，见 .github/workflows/
release.yml 里 linux job 的注释），但组装这一步（收集许可证、拼 README、打
tar.gz）跟平台没关系——拆开之后这部分能在任何装了 Python 3 的机器上单独验证，
不用每次改一行 README 文案就重新在 Linux 上编一遍 Easel。

    cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release \\
          -DEASEL_BUILD_EXAMPLES=OFF -DEASEL_BUILD_TESTS=OFF
    cmake --build build/linux-release --target easel_workbench --parallel
    cmake --install build/linux-release --prefix build/linux-prebuilt
    python3 scripts/make_tarball_linux.py \\
          --exe build/linux-release/easel --prebuilt-dir build/linux-prebuilt

产出 build/pack/Easel-<版本>-linux-x64.tar.gz，解开是：

    Easel-0.1.1-linux-x64/
    ├── bin/
    │   └── easel              工作台可执行文件（cmake --install 之外单独拷的那份，
    │                           跟 easel_workbench 是同一次构建出来的，不是重新编的）
    ├── easel/                  源码树 + prebuilt/（cmake --install 的 prefix）+ VERSION.json
    ├── README.txt
    ├── LICENSE.txt
    └── licenses/                vendor/*/LICENSE* 收的三方许可证

为什么可执行文件放进 bin/、不直接摆在包根：src/editor.cpp 的 resolvePaths() 找
Easel 源码靠「就近优先」的 exe-up 规则——从可执行文件所在目录开始，一路往上翻
最多 6 层，找一个叫 easel/ 且像真源码树（有 CMakeLists.txt 和
include/easel/easel.h）的目录（Linux 没有 macOS bundle 那条专属分支，走的就是
这条通用规则）。可执行文件如果直接摆在包根、跟 easel/ 源码目录并排，两者会撞
名——Linux 上可执行文件没有 .exe 后缀，"easel"（文件）和 "easel"（目录）不能
同时待在同一层目录里。摆进 bin/ 子目录之后：exeDir() 是 <包根>/bin，第 0 层
候选 <包根>/bin/easel 不存在（正常跳过），第 1 层候选 <包根>/easel 就是源码树，
两层之内命中，稳稳落在 6 层预算以内，不用给 Linux 单开一条平台分支。
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime, timezone

# scripts/abi.py 就在这个文件旁边——ABI 指纹算法只有这一份实现。这里用不上直接
# 调用它：cmake --install 已经把 abi 算好写进了 --prebuilt-dir/easel-prebuilt.json，
# 直接抄过来即可（预编译包和源码树是同一次构建产的，天然一致）。

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 打进 <包根>/easel/ 的东西——跟 make_bundle_mac.py 的 EASEL_ITEMS 一字不差：
# 学生工程走的是 --new 生成的 .easel/CMakeLists.txt（直接 -DEASEL_DIR=<路径>），
# 不需要 Easel 自己的 preset 或工作台源码；但 dist/（单头库摊平版）必须带上，
# src/new_project.cpp 生成的 CMakeLists 里 EASEL_CORE_DIR = ${EASEL_DIR}/dist。
EASEL_ITEMS = ["CMakeLists.txt", "LICENSE", "cmake", "include", "src", "vendor", "assets",
               "dist", "template", "template-hello", "examples", "docs"]

README_TXT = """Easel {version} · Linux 发布包（x86_64）

解压后直接跑（不用装 Easel、不用管理员权限）：
    ./bin/easel

打不开、或者画面不对，跑一下（把输出发给维护者）：
    ./bin/easel --doctor

命令行用法：
    ./bin/easel --new ~/projects --name Demo     # 新建工程（工程名只能用英文字母/数字/下划线）
    ./bin/easel --build ~/projects/Demo           # 编译
    ./bin/easel --run ~/projects/Demo             # 运行

想更好敲，可以自己软链到 PATH 里（注意目标名一定是 bin/easel，不是包根）：
    ln -s "$PWD/bin/easel" ~/.local/bin/easel

前置条件（Ubuntu / Debian 系；其它发行版按包管理器换等价包名）：

1. 只是先跑跑看、看看画面（不编译任何东西）——需要下面这些**运行时**共享库
   （多数带桌面环境的机器上早就装好了，只有精简安装 / 容器 / 服务器版才要手动装）：
       sudo apt-get install -y libgl1 libx11-6 libxrandr2 libxinerama1 \\
           libxcursor1 libxi6 libxkbcommon0 libgtk-3-0

2. 要编译（新建工程之后「编译并运行」，哪怕链的是包里带的预编译库，链接这一步
   仍然要在本机核对 OpenGL / X11 / GTK 这些库存不存在）——需要下面这些
   **-dev** 包（比运行时那份多了链接用的无版本号 .so，装完才能跟其它平台一样
   几秒钟链上，不用等三分钟从源码编）：
       sudo apt-get install -y g++ cmake libgl1-mesa-dev libx11-dev libxrandr-dev \\
           libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libgtk-3-dev
   `ninja-build` 可选（`sudo apt-get install -y ninja-build`）——没装的话生成的构建
   脚本会自动退回 Unix Makefiles，一样能编，只是快慢有点差别。

新建的工程默认放在 ~/projects。

包里的 easel/ 目录是给 Easel 自己用的源码树（bin/easel 靠它找到 dist/、模板、
预编译库），不是给你改的地方——你的工程建在别处（比如 ~/projects），跟这份包
本身分开。
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
    """把 Easel 自己的 LICENSE 和 vendor/*/LICENSE* 收进 dest（跟 make_bundle_mac.py /
    package.py 的 collect_licenses 是同一套办法）。"""
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


def copy_prebuilt(src, dest):
    """把 cmake --install 装出来的 prefix 拷进 <包根>/easel/prebuilt/。

    src 得是 `cmake --install <build_dir> --prefix <src>` 的产物：里面要有
    lib/cmake/easel/easelConfig.cmake（学生工程 find_package(easel CONFIG) 认的
    那份）和 easel-prebuilt.json（版本戳，跟源码树的 VERSION.json 核对 abi）。
    """
    cfg = os.path.join(src, "lib", "cmake", "easel", "easelConfig.cmake")
    stamp = os.path.join(src, "easel-prebuilt.json")
    if not os.path.exists(cfg):
        raise SystemExit(f"[发布包] {src} 不像是 Easel 的安装目录（没有 {cfg}）")
    if not os.path.exists(stamp):
        raise SystemExit(f"[发布包] {src} 没有 easel-prebuilt.json（不像是 cmake --install 装出来的）")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest, symlinks=False, ignore=shutil.ignore_patterns(".DS_Store"))
    return dest


def check_exe(path):
    """粗粗核对一下 --exe 传的真是个 Linux（ELF）可执行文件——不是为了防坏人，
    是为了防手滑（比如在 Mac 上顺手传了个本机编的 easel 进来，包名却叫 linux-x64，
    学生解压出来一运行才发现「不是这个系统的程序」，比在打包这一步就报错难查）。
    """
    if not os.path.isfile(path):
        raise SystemExit(f"[发布包] --exe 指的文件不存在：{path}")
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic != b"\x7fELF":
        raise SystemExit(f"[发布包] {path} 不是 ELF 可执行文件（开头是 {magic!r}）——"
                          f"传错平台编的 easel 了？这个脚本只组装 Linux 包")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, metavar="路径",
                     help="已经在 Linux 上编好的工作台可执行文件（build/<preset>/easel）")
    ap.add_argument("--prebuilt-dir", required=True, metavar="路径",
                     help="cmake --install 装出来的 prefix（--exe 跟它必须是同一次构建产的，"
                          "否则 easel-prebuilt.json 里的 abi 会跟源码树核对不上）")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "pack"))
    ap.add_argument("--no-tar", action="store_true")
    args = ap.parse_args()

    check_exe(args.exe)

    if not os.path.isdir(os.path.join(ROOT, "vendor")):
        print("[发布包] 没有 vendor/ —— 先跑 python3 scripts/vendor.py", file=sys.stderr)
        return 2

    version = easel_version()
    name = f"Easel-{version}-linux-x64"
    out_root = os.path.join(os.path.abspath(args.out), name)

    shutil.rmtree(out_root, ignore_errors=True)
    os.makedirs(out_root)

    print("=== 1/5 可执行文件 → bin/easel ===")
    # 单放一个 bin/ 子目录，不跟包根平摆——包根还要放 easel/ 源码目录，Linux 上
    # 可执行文件没有 .exe 后缀，"easel"（文件）和 "easel"（目录）没法在同一层
    # 共存。摆进 bin/ 之后 exeDir() 是 <包根>/bin，往上翻一层就是包根，找到
    # <包根>/easel——两层之内命中，src/editor.cpp 的 exe-up 规则最多给 6 层预算。
    bin_dir = os.path.join(out_root, "bin")
    os.makedirs(bin_dir)
    exe_dest = os.path.join(bin_dir, "easel")
    shutil.copy2(args.exe, exe_dest)
    os.chmod(exe_dest, 0o755)

    print("=== 2/5 源码树 → easel/ ===")
    resources_easel = os.path.join(out_root, "easel")
    missing = copy_easel_tree(resources_easel)
    if missing:
        print(f"  （跳过不存在的：{', '.join(missing)}）")

    print("=== 3/5 预编译包 → easel/prebuilt/ ===")
    prebuilt_dir = os.path.join(resources_easel, "prebuilt")
    copy_prebuilt(args.prebuilt_dir, prebuilt_dir)
    print(f"    {dir_size(prebuilt_dir)/1e6:.0f} MB")

    commit = git_commit(ROOT)
    # 直接抄 --prebuilt-dir 里 cmake --install 写的 easel-prebuilt.json 里的
    # abi——那份和 --exe 是同一次构建产的，天然一致，不用再调 abi.py 重算一遍。
    abi = "unknown"
    prebuilt_stamp = os.path.join(prebuilt_dir, "easel-prebuilt.json")
    if os.path.exists(prebuilt_stamp):
        with open(prebuilt_stamp, encoding="utf-8") as f:
            abi = json.load(f).get("abi", "unknown")
    write_version_json(os.path.join(resources_easel, "VERSION.json"), commit, abi)
    print(f"  源码戳 → easel/VERSION.json（commit {commit or 'unknown'}，abi {abi}）")

    print("=== 4/5 许可证 + README ===")
    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(out_root, "LICENSE.txt"))
    nlic = collect_licenses(os.path.join(out_root, "licenses"))
    print(f"  licenses/ 收了 {nlic} 份")

    with open(os.path.join(out_root, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README_TXT.format(version=version))

    print(f"    展开后 {dir_size(out_root)/1e6:.0f} MB")

    if args.no_tar:
        print(f"好了：{out_root}")
        return 0

    print("=== 5/5 打包（tar.gz）===")
    tar_path = os.path.join(os.path.abspath(args.out), f"{name}.tar.gz")
    if os.path.exists(tar_path):
        os.remove(tar_path)
    # arcname 统一加 name/ 这层前缀，解压出来跟 out_root 同名的一层目录，跟
    # macOS/Windows 那两份包解压出来「顶层就是一个目录」的心智模型一致。
    with tarfile.open(tar_path, "w:gz") as tar:
        tar.add(out_root, arcname=name)
    print(f"好了：{tar_path}（{os.path.getsize(tar_path)/1e6:.0f} MB）")
    print("解压后：tar xzf " + os.path.basename(tar_path) + " && ./" + name + "/bin/easel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
