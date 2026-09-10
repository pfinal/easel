#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装 Windows 免安装工具箱 —— 在 Mac 上跑也行（本来就是在 Mac 上开发的）。

    python3 scripts/vendor.py         # 1. 先把依赖抓下来
    python3 scripts/make_toolbox.py   # 2. 再组装

带上 CI 产的 Windows 预编译包（工程就不用从源码编 ImGui/GLFW 了，D-26）：

    python3 scripts/make_toolbox.py --prebuilt-dir <MinGW 装出来的 prefix>

产出 windows-green/代码酷C++工具箱.zip。解压后，双击 启动.bat 就能用：
不装任何东西、不要管理员权限、不写注册表、不用联网。

三个组件都是**纯 zip**（不是 7z 自解压），所以这个脚本在 Mac / Linux / Windows 上
都能跑，目标机器上也不需要任何解压工具。

要升级版本就改下面的 COMPONENTS。
"""
import argparse
import io
import os
import shutil
import sys
import urllib.request
import zipfile

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
    ("cmake", "cmake-4.4.3-windows-x86_64.zip",
     "https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-windows-x86_64.zip",
     "cmake-4.4.3-windows-x86_64"),
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

WORKBENCH_BAT = """@echo off
rem 代码酷工作台 —— 主入口（新建工程 / 编译 / 运行 / 生成 exe / 导出源码）
rem 本文件必须用 GBK 存、不要加 chcp（理由见 启动.bat）。
title daimaku workbench
set "KIT=%~dp0"
if not exist "%KIT%cmake\\bin\\cmake.exe" ( echo   没找到 %KIT%cmake\\bin\\cmake.exe —— 解压不完整？请重新解压整个 zip。 & goto :fail )
if not exist "%KIT%ninja\\ninja.exe" ( echo   没找到 %KIT%ninja\\ninja.exe —— 解压不完整？ & goto :fail )
if not exist "%KIT%w64devkit\\bin\\g++.exe" ( echo   没找到 %KIT%w64devkit\\bin\\g++.exe —— 解压不完整？ & goto :fail )
set "PATH=%KIT%w64devkit\\bin;%KIT%cmake\\bin;%KIT%ninja;%PATH%"
set "WB=%KIT%easel\\build\\mingw\\workbench.exe"
if not exist "%WB%" (
    echo.
    echo   第一次启动，要先把工作台编出来，几分钟。以后就直接开了。
    echo   工具箱目录：%KIT%
    echo.
    "%KIT%cmake\\bin\\cmake.exe" -S "%KIT%easel" -B "%KIT%easel\\build\\mingw" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEASEL_BUILD_EXAMPLES=OFF -DEASEL_BUILD_TESTS=OFF -DCMAKE_C_COMPILER="%KIT%w64devkit\\bin\\gcc.exe" -DCMAKE_CXX_COMPILER="%KIT%w64devkit\\bin\\g++.exe" -DCMAKE_MAKE_PROGRAM="%KIT%ninja\\ninja.exe"
    if errorlevel 1 goto :fail
    "%KIT%cmake\\bin\\cmake.exe" --build "%KIT%easel\\build\\mingw" --target workbench --parallel
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
rem 本文件必须用 GBK 存、不要加 chcp（理由见 启动.bat）。
title daimaku prebuilt export
setlocal
set "KIT=%~dp0"
set "OUT=%KIT%prebuilt-mingw"
set "PATH=%KIT%w64devkit\\bin;%KIT%cmake\\bin;%KIT%ninja;%PATH%"
if not exist "%KIT%easel\\build\\mingw\\workbench.exe" (
    echo.
    echo   还没有编过工作台。先双击 工作台.bat，编完再来。
    goto :fail
)
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
echo.
echo   [1/3] 工作台 workbench.exe
copy /y "%KIT%easel\\build\\mingw\\workbench.exe" "%OUT%\\" >nul
echo   [2/3] 预编译的 Easel 包（cmake --install，十几 MB）
"%KIT%cmake\\bin\\cmake.exe" --install "%KIT%easel\\build\\mingw" --prefix "%OUT%\\prebuilt"
if errorlevel 1 goto :fail
echo   [3/3] 版本信息
> "%OUT%\\版本.txt" (
    echo 工具链: w64devkit gcc 14.1 + Ninja
    echo 日期: %DATE% %TIME%
    findstr /c:"define EASEL_VERSION" "%KIT%easel\\include\\easel\\core.h"
    "%KIT%w64devkit\\bin\\g++.exe" --version
)
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
echo   ** 一般不用这个黑框：双击目录里的 工作台.bat 就行 **
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "windows-green", "工具箱"))
    ap.add_argument("--no-zip", action="store_true")
    ap.add_argument("--prebuilt-dir", metavar="路径",
                    help="一份编好的 Easel 安装目录（cmake --install 的 prefix），"
                         "拷进 easel/prebuilt/。必须是 Windows/MinGW 编的，由 CI 产。"
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

    # 预编译包（D-26）：有它工程 find_package(easel CONFIG) 几秒钟就链上
    if args.prebuilt_dir:
        dest = os.path.join(out, "easel", "prebuilt")
        try:
            size = copy_prebuilt(args.prebuilt_dir, dest)
        except (OSError, RuntimeError) as e:
            print(f"[工具箱] 预编译包拷不进来：{e}", file=sys.stderr)
            return 2
        print(f"  预编译的 Easel → easel/prebuilt（{size/1e6:.0f} MB）")
    else:
        print("  没给 --prebuilt-dir：工具箱里不带预编译的 Easel，"
              "第一次编译要等三分钟。Windows 的那份由 CI 产。")

    print("=== 3/3 启动脚本 ===")
    # .bat 必须是 GBK + CRLF、不带 chcp（见 START_BAT 里的注释）
    for name in ("启动.bat", "start.bat"):   # 中文名万一被解压器搞乱，留个 ASCII 的
        with open(os.path.join(out, name), "w", encoding="gbk", newline="\r\n") as f:
            f.write(START_BAT)
    # 工作台才是主入口（D-29）；命令行那个黑框留给要手动折腾的人
    for name in ("工作台.bat", "workbench.bat"):
        with open(os.path.join(out, name), "w", encoding="gbk", newline="\r\n") as f:
            f.write(WORKBENCH_BAT)
    # 预编译导出脚本（把编好的东西打包发给维护者）
    for name in ("导出预编译.bat", "export-prebuilt.bat"):
        with open(os.path.join(out, name), "w", encoding="gbk", newline="\r\n") as f:
            f.write(PREBUILT_BAT)
    readme = os.path.join(ROOT, "windows-green", "README.md")
    if os.path.exists(readme):
        shutil.copy2(readme, os.path.join(out, "使用说明.md"))

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
    print("解压后，双击 启动.bat。路径别太深，Windows 有 260 字符限制。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
