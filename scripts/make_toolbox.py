#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装 Windows 免安装工具箱 —— 在 Mac 上跑也行（本来就是在 Mac 上开发的）。

    python3 scripts/vendor.py         # 1. 先把依赖抓下来
    python3 scripts/make_toolbox.py   # 2. 再组装

带上 CI 产的 Windows 预编译包（工程就不用从源码编 ImGui/GLFW 了，D-26）：

    python3 scripts/make_toolbox.py --prebuilt-dir <MinGW 装出来的 prefix>

产出 windows-green/代码酷C++工具箱.zip。解压后，双击 start.bat 就能用：
不装任何东西、不要管理员权限、不写注册表、不用联网。

三个组件都是**纯 zip**（不是 7z 自解压），所以这个脚本在 Mac / Linux / Windows 上
都能跑，目标机器上也不需要任何解压工具。

要升级版本就改下面的 COMPONENTS。
"""
import argparse
import io
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from datetime import datetime, timezone

# Windows 上标准输出默认 cp1252，打中文会炸；统一成 UTF-8（Python 3.7+）
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, ".cache")

# 全部锁死版本，保证每次组出来的包一模一样。
COMPONENTS = [
    # (目录名, 文件名, 下载地址, 压缩包里要剥掉的顶层目录)
    ("w64devkit", "w64devkit-1.23.0.zip",
     "https://github.com/skeeto/w64devkit/releases/download/v1.23.0/w64devkit-1.23.0.zip",
     "w64devkit"),
    ("cmake", "cmake-3.31.6-windows-x86_64.zip",
     "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip",
     "cmake-3.31.6-windows-x86_64"),
    ("ninja", "ninja-win-1.13.2.zip",
     "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
     None),
]

# Easel 里要打进包的东西（build/ 之类不进去）
# cmake/ 里是 easelConfig.cmake.in —— 工具箱里第一次启动会以顶层工程的身份配置
# Easel（工作台.bat），那时 EASEL_INSTALL 默认是 ON，要用到这个模板。
EASEL_ITEMS = ["CMakeLists.txt", "CMakePresets.json", "LICENSE", "README.md",
               "include", "src", "cmake", "scripts", "assets", "examples", "template",
               "template-hello", "workbench", "docs", "tests", "dist", "vendor"]

EASEL_BAT = """@echo off
rem 代码酷工作台 —— 主入口（新建工程 / 编译 / 运行 / 生成 exe / 导出源码）
rem 本文件必须用 GBK 存、不要加 chcp（理由见 start.bat）。
title daimaku Easel
set "KIT=%~dp0"
if exist "%KIT%Easel.exe" (
    start "" "%KIT%Easel.exe"
    goto :eof
)
if not exist "%KIT%cmake\\bin\\cmake.exe" ( echo   没找到 %KIT%cmake\\bin\\cmake.exe —— 解压不完整？请重新解压整个 zip。 & goto :fail )
if not exist "%KIT%ninja\\ninja.exe" ( echo   没找到 %KIT%ninja\\ninja.exe —— 解压不完整？ & goto :fail )
if not exist "%KIT%w64devkit\\bin\\g++.exe" ( echo   没找到 %KIT%w64devkit\\bin\\g++.exe —— 解压不完整？ & goto :fail )
set "PATH=%KIT%w64devkit\\bin;%KIT%cmake\\bin;%KIT%ninja;%PATH%"
set "WB=%KIT%easel\\build\\mingw\\Easel.exe"
if not exist "%WB%" (
    echo.
    echo   第一次启动，要先把工作台编出来，几分钟。以后就直接开了。
    echo   工具箱目录：%KIT%
    echo.
    "%KIT%cmake\\bin\\cmake.exe" -S "%KIT%easel" -B "%KIT%easel\\build\\mingw" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEASEL_BUILD_EXAMPLES=OFF -DEASEL_BUILD_TESTS=OFF -DCMAKE_C_COMPILER="%KIT%w64devkit\\bin\\gcc.exe" -DCMAKE_CXX_COMPILER="%KIT%w64devkit\\bin\\g++.exe" -DCMAKE_MAKE_PROGRAM="%KIT%ninja\\ninja.exe"
    if errorlevel 1 goto :fail
    "%KIT%cmake\\bin\\cmake.exe" --build "%KIT%easel\\build\\mingw" --target easel_workbench --parallel
    if errorlevel 1 goto :fail
)
start "" "%WB%"
goto :eof

:fail
echo.
echo   编不出来。把上面的报错整段复制下来求助。
pause
"""

PREBUILT_BAT = """@echo off
rem 把编好的东西收进一个目录，打包发给维护者（或者放回工具箱里）。
rem 本文件必须用 GBK 存、不要加 chcp（理由见 start.bat）。
title daimaku prebuilt export
setlocal
set "KIT=%~dp0"
set "OUT=%KIT%prebuilt-mingw"
set "PATH=%KIT%w64devkit\\bin;%KIT%cmake\\bin;%KIT%ninja;%PATH%"
if not exist "%KIT%easel\\build\\mingw\\Easel.exe" (
    echo.
    echo   还没有编过工作台。先双击 Easel.bat，编完再来。
    goto :fail
)
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
echo.
echo   [1/2] 工作台 Easel.exe
copy /y "%KIT%easel\\build\\mingw\\Easel.exe" "%OUT%\\" >nul
echo   [2/2] 预编译的 Easel 包（cmake --install，十几 MB；连版本戳 easel-prebuilt.json 一起装）
"%KIT%cmake\\bin\\cmake.exe" --install "%KIT%easel\\build\\mingw" --prefix "%OUT%\\prebuilt"
if errorlevel 1 goto :fail
echo.
echo   好了：%OUT%
echo   把这个目录整个压成 zip 发给维护者就行。
goto :end

:fail
echo.
echo   失败了。把上面的输出整段复制下来求助。

:end
echo.
pause
endlocal
"""

START_BAT = """@echo off
rem 代码酷 C++ 工具箱
rem 本文件必须用 GBK 存、不要加 chcp —— cmd.exe 在批处理里切代码页
rem 会把解析器的文件偏移弄错位，往后每一行都会被啃掉几个字节。
title daimaku C++ toolbox
set "KIT=%~dp0"
set "PATH=%KIT%w64devkit\\bin;%KIT%cmake\\bin;%KIT%ninja;%PATH%"
echo.
echo   代码酷 C++ 工具箱
echo   ---------------------------------------------------------------
g++ --version   | findstr /r /c:"^g++"
cmake --version | findstr /r /c:"^cmake"
ninja --version
echo.
echo   模板工程在 %KIT%easel\\template
echo.
echo   ** 一般不用这个黑框：双击目录里的 Easel.bat 就行 **
echo      （新建工程、编译、运行、生成 exe、导出源码，都在里面）
echo.
echo   第一次用，先把模板拷成自己的工程：
echo       xcopy /E /I "%KIT%easel\\template" "%KIT%我的作品"
echo.
echo   写算法（不用 CMake，一条命令）：
echo       cd 我的作品
echo       g++ -std=c++17 -DEASEL_STANDALONE src\\solver.cpp -o solver.exe
echo       solver.exe data\\example.json
echo.
echo   做界面：双击工程里的 跑.bat，或者
echo       cmake --preset mingw -DEASEL_DIR=..\\easel
echo       cmake --build --preset mingw
echo       build\\mingw\\bin\\app.exe --open data\\example.json --solve
echo   ---------------------------------------------------------------
echo.
cd /d "%KIT%"
cmd /k
"""


def fetch(url, name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if os.path.exists(path):
        print(f"  已缓存 {name}（{os.path.getsize(path)/1e6:.1f} MB）")
        return path
    print(f"  下载 {name} …")
    urllib.request.urlretrieve(url, path)
    print(f"        {os.path.getsize(path)/1e6:.1f} MB")
    return path


def unzip(archive, dest, strip_top):
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            name = info.filename
            if strip_top:
                prefix = strip_top.rstrip("/") + "/"
                if not name.startswith(prefix):
                    continue
                name = name[len(prefix):]
            if not name or name.endswith("/"):
                continue
            target = os.path.realpath(os.path.join(dest, name))
            if not target.startswith(os.path.realpath(dest)):
                raise RuntimeError(f"压缩包里有可疑路径：{info.filename}")
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out)


def copy_easel(dest):
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


def resolve_prebuilt_layout(path):
    """--prebuilt-dir 认两种布局：

    1. 「导出目录」（export-prebuilt.bat 产出，或从 Windows 虚拟机整个拷出来的）：
           <path>/Easel.exe
           <path>/prebuilt/easel-prebuilt.json  ← cmake --install 的 prefix
    2. 「直接给 install prefix」（自己手动 cmake --install 出来的，没有 exe）：
           <path>/easel-prebuilt.json

    返回 (install_prefix_dir, exe_path_or_None)：install_prefix_dir 是那份带
    include/ lib/ 的 cmake --install 目录，exe_path 是同级的 Easel.exe（布局 2
    没有 exe，就是 None）。两种布局都认不出就报错。
    """
    variant1_prefix = os.path.join(path, "prebuilt")
    if os.path.exists(os.path.join(variant1_prefix, "easel-prebuilt.json")):
        exe = os.path.join(path, "Easel.exe")
        return variant1_prefix, (exe if os.path.isfile(exe) else None)
    if os.path.exists(os.path.join(path, "easel-prebuilt.json")):
        return path, None
    raise RuntimeError(
        f"{path} 认不出布局：既没有 {os.path.join(path, 'easel-prebuilt.json')}，"
        f"也没有 {os.path.join(variant1_prefix, 'easel-prebuilt.json')}"
        f"（cmake --install 装出来的东西该在其中一处，重新装一遍再来）")


def copy_prebuilt(src, dest):
    """把一份预编译好的 Easel 包放进 <工具箱>/easel/prebuilt/。

    src 是 `cmake --install --prefix <src>` 产出的目录，里面是 include/ lib/
    lib/cmake/easel/easelConfig.cmake。工程的 .easel/CMakeLists.txt 会先看
    ${EASEL_DIR}/prebuilt/lib/cmake/easel/easelConfig.cmake 在不在，在就
    find_package(easel CONFIG)，几秒钟链上，不再从源码编 ImGui/GLFW（三分钟）。

    包必须是**目标平台**的：工具箱是给 Windows 平台用的，里面这份得是 MinGW 编的，
    所以由 CI 在 Windows 上产、再用 --prebuilt-dir 传进来。Mac 上跑这个脚本时不给
    这个参数就行 —— 少了它工具箱照样能用，只是第一次编译要等三分钟。
    """
    cfg = os.path.join(src, "lib", "cmake", "easel", "easelConfig.cmake")
    if not os.path.exists(cfg):
        raise RuntimeError(f"{src} 不像是 Easel 的安装目录（没有 {cfg}）")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest, symlinks=False, ignore=shutil.ignore_patterns(".DS_Store"))
    return dir_size(dest)


def dir_size(path):
    return sum(os.path.getsize(os.path.join(dp, f))
               for dp, _, fs in os.walk(path) for f in fs)


def git_commit(repo):
    """repo 里跑 `git rev-parse --short=7 HEAD`。不是 git 仓库 / 没装 git 就返回 None。"""
    try:
        out = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=repo,
                              capture_output=True, text=True, check=True)
        return out.stdout.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def easel_version():
    """从根 CMakeLists.txt 的 project(easel VERSION x.y.z ...) 里取版本号。"""
    cmake = os.path.join(ROOT, "CMakeLists.txt")
    text = open(cmake, encoding="utf-8").read()
    m = re.search(r"project\(\s*easel\s+VERSION\s+([0-9.]+)", text)
    return m.group(1) if m else "0.0.0"


def write_version_json(dest, commit):
    """<工具箱>/easel/VERSION.json —— 源码树的版本戳，字段和 easel-prebuilt.json 对齐。

    make_toolbox.py 打包出来的是源码，不是编出来的东西，所以 compiler / system 留空；
    src/new_project.cpp 的 kCMake 拿它的 commit 跟 easel/prebuilt/easel-prebuilt.json
    的 commit 核对，不一致就改用源码编，不会悄悄链上一份对不上号的预编译包。
    """
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


def check_prebuilt_commit(prebuilt_dir, source_commit):
    """--prebuilt-dir 必须带着 cmake --install 写的 easel-prebuilt.json，且要过两条校验：

    1. system 必须是 Windows —— 工具箱是给 Windows 机器用的，这份预编译包却是
       随便一个平台编的都能通过 commit 校验（commit 只跟源码版本有关，跟编译
       平台无关），必须在这里单独挡掉，不然会把 Mac/Linux 编的包误塞进 Windows
       工具箱，装进去的 Easel.exe / .a 目标机器根本跑不起来（或者链接不上）。
    2. commit 得和正在打包的源码 commit 一样 —— 不然工具箱里预编译的 Easel
       和源码树不是同一份东西，最难查的一类问题。
    """
    stamp = os.path.join(prebuilt_dir, "easel-prebuilt.json")
    if not os.path.exists(stamp):
        raise RuntimeError(f"{prebuilt_dir} 里没有 easel-prebuilt.json（不像是 `cmake --install` 装出来的，"
                            f"重新 cmake --install 一遍再来）")
    with open(stamp, encoding="utf-8") as f:
        meta = json.load(f)
    system = meta.get("system")
    if system != "Windows":
        raise RuntimeError(
            f"{stamp} 里 system 是 {system!r}，不是 \"Windows\" —— 这份预编译包不是 Windows/MinGW "
            f"编的，不能塞进 Windows 工具箱（目标机器上会跑不起来），换一份在 Windows 上 "
            f"cmake --install 出来的再来")
    prebuilt_commit = meta.get("commit")
    if not source_commit:
        raise RuntimeError("打包用的这份 Easel 源码不是 git 仓库（或者没装 git），测不出 commit，"
                            "没法核对预编译包是不是同一份源码编的")
    if prebuilt_commit != source_commit:
        raise RuntimeError(f"预编译包是 {prebuilt_commit} 编的，源码是 {source_commit}，重编一份再来")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "windows-green", "工具箱"))
    ap.add_argument("--no-zip", action="store_true")
    ap.add_argument("--prebuilt-dir", metavar="路径",
                    help="一份编好的 Easel，认两种布局：export-prebuilt.bat 产出的导出目录"
                         "（<路径>/Easel.exe + <路径>/prebuilt/，两者都拷进工具箱）"
                         "，或者直接给 cmake --install 的 prefix（<路径>/easel-prebuilt.json，"
                         "没有 exe）。必须是 Windows/MinGW 编的，由 CI 或虚拟机产。"
                         "不给就跳过 —— 第一次编译要多等三分钟，别的都一样。")
    args = ap.parse_args()

    out = args.out
    os.makedirs(out, exist_ok=True)

    print("=== 1/3 下载并解开三个组件 ===")
    for name, fname, url, strip in COMPONENTS:
        path = fetch(url, fname)
        print(f"  解开 {name}")
        unzip(path, os.path.join(out, name), strip)

    gpp = os.path.join(out, "w64devkit", "bin", "g++.exe")
    if not os.path.exists(gpp):
        print(f"[工具箱] 解完没找到 {gpp}，压缩包结构可能变了", file=sys.stderr)
        return 2

    print("=== 2/3 放进 Easel 与依赖源码 ===")
    if not os.path.isdir(os.path.join(ROOT, "vendor")):
        print("[工具箱] 没有 vendor/ —— 先跑 python3 scripts/vendor.py，"
              "不然目标机器上构建时还要联网", file=sys.stderr)
        return 2
    missing = copy_easel(os.path.join(out, "easel"))
    if missing:
        print(f"  （跳过不存在的：{', '.join(missing)}）")

    source_commit = git_commit(ROOT)
    write_version_json(os.path.join(out, "easel", "VERSION.json"), source_commit)
    print(f"  源码戳 → easel/VERSION.json（commit {source_commit or 'unknown'}）")

    # 预编译包（D-26）：有它工程 find_package(easel CONFIG) 几秒钟就链上
    exe_dest = os.path.join(out, "Easel.exe")
    if os.path.exists(exe_dest):
        os.remove(exe_dest)  # 上一次组装可能留下的，这次没带就别让它悄悄留在包里
    have_prebuilt_exe = False
    if args.prebuilt_dir:
        try:
            prebuilt_prefix, exe_path = resolve_prebuilt_layout(args.prebuilt_dir)
            check_prebuilt_commit(prebuilt_prefix, source_commit)
        except RuntimeError as e:
            print(f"[工具箱] {e}", file=sys.stderr)
            return 2
        dest = os.path.join(out, "easel", "prebuilt")
        try:
            size = copy_prebuilt(prebuilt_prefix, dest)
        except (OSError, RuntimeError) as e:
            print(f"[工具箱] 预编译包拷不进来：{e}", file=sys.stderr)
            return 2
        print(f"  预编译的 Easel → easel/prebuilt（{size/1e6:.0f} MB）")
        if exe_path:
            shutil.copy2(exe_path, exe_dest)
            have_prebuilt_exe = True
            print(f"  预编译主程序 → Easel.exe（{os.path.getsize(exe_dest)/1e6:.0f} MB）")
    else:
        print("  没给 --prebuilt-dir：工具箱里不带预编译的 Easel，"
              "第一次编译要等三分钟。Windows 的那份由 CI 产。")

    print("=== 3/3 启动脚本 ===")
    # .bat 只留英文名（D-36）；必须是 GBK + CRLF、不带 chcp（见 START_BAT 里的注释）
    with open(os.path.join(out, "start.bat"), "w", encoding="gbk", newline="\r\n") as f:
        f.write(START_BAT)
    # 工作台才是主入口（D-29）；命令行那个黑框留给要手动折腾的人
    with open(os.path.join(out, "Easel.bat"), "w", encoding="gbk", newline="\r\n") as f:
        f.write(EASEL_BAT)
    # 预编译导出脚本（把编好的东西打包发给维护者）
    with open(os.path.join(out, "export-prebuilt.bat"), "w", encoding="gbk", newline="\r\n") as f:
        f.write(PREBUILT_BAT)
    readme = os.path.join(ROOT, "windows-green", "README.md")
    if os.path.exists(readme):
        shutil.copy2(readme, os.path.join(out, "README.md"))

    print(f"    展开后 {dir_size(out)/1e6:.0f} MB")

    if args.no_zip:
        print(f"好了：{out}")
        return 0

    zip_path = os.path.join(ROOT, "windows-green", "代码酷C++工具箱.zip")
    print("=== 打包（要等一会儿）===")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for dp, _, fs in os.walk(out):
            for f in fs:
                full = os.path.join(dp, f)
                z.write(full, os.path.relpath(full, out))
    print(f"好了：{zip_path}（{os.path.getsize(zip_path)/1e6:.0f} MB）")
    if have_prebuilt_exe:
        print("解压后，双击 Easel.exe 或 Easel.bat 直接开。路径别太深，Windows 有 260 字符限制。")
    else:
        print("解压后，双击 start.bat。路径别太深，Windows 有 260 字符限制。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
