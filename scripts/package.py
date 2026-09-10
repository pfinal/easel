#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一键出发布包：Release 的 exe + 完整源码 zip + 第三方许可证 + 对照发布清单自检。

    python3 scripts/package.py ../我的作品 --name 我的作品

产出（在工程目录的 dist/ 下）：
    我的作品-程序/            可双击运行：exe + assets + data + 运行说明.txt
    我的作品-程序.zip
    我的作品-源码.zip         源码，不含 build/
    检查清单.txt              对照发布清单逐条核对的结果

**要在哪个系统上运行，就在哪个系统上打包**（HANDOFF 里 10/8 那个检查点就是干这个的）。
Mac 上跑是用来验流程的。
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile

# 源码包里要装什么。子目录会整个拷进去（build 之类在 SKIP 里排掉）。
SOURCE_ITEMS = ["src", "tests", "data", "assets", "debug",
                "CMakeLists.txt", "CMakePresets.json", "README.md",
                ".vscode", ".github", "跑.bat", "跑.sh", "run.bat"]
SKIP = shutil.ignore_patterns("build", "dist", ".git", "__pycache__", "*.pyc",
                              ".DS_Store", ".gitkeep", "*.o", "*.obj", "*.exe",
                              "a.out", "solver", "solver.exe", "imgui.ini")

RUN_NOTE = """{name}

双击 {exe} 运行。

Windows 上如果弹出蓝色的「Windows 已保护你的电脑」：
  点「更多信息」-> 「仍要运行」。
  这是 SmartScreen 对没有购买代码签名证书的新程序的默认提示，不是病毒。

macOS 上如果提示「无法验证开发者」：
  右键点它 -> 打开 -> 再点一次「打开」。

程序打不开、或者画面不对，请在命令行里跑：
  {exe} --doctor
把输出发过来，里面有显卡、后端、DPI、字体、工作目录等全部信息。
"""


def run(cmd, cwd=None):
    print("   ", " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        raise SystemExit(f"[打包] 这条命令失败了：{' '.join(str(c) for c in cmd)}")


def find_exe(build_dir, names=("app", "app.exe")):
    for dp, _, fs in os.walk(build_dir):
        for f in fs:
            if f in names:
                return os.path.join(dp, f)
    return None


def zip_dir(src, zip_path, arc_root):
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for dp, _, fs in os.walk(src):
            for f in fs:
                full = os.path.join(dp, f)
                z.write(full, os.path.join(arc_root, os.path.relpath(full, src)))
    return os.path.getsize(zip_path)


BUNDLE_MARKER = "[easel:bundled]"
BUNDLE_BLOCK = """# ---- 包里自带的 Easel  [easel:bundled] ----------------------------------
# 源码包里有 easel/ 就直接用它：不联网、不用装、不用加任何 cmake 参数。
if(NOT DEFINED EASEL_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/easel/CMakeLists.txt")
  set(EASEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/easel")
  message(STATUS "用包里自带的 Easel: ${EASEL_DIR}")
endif()

"""


def ensure_bundle_block(cmake_path):
    """让源码包里的顶层 CMakeLists 认得同一个包里的 easel/（和 app 的「导出工程」一致，D-28）。"""
    try:
        text = open(cmake_path, encoding="utf-8").read()
    except OSError:
        return False
    if BUNDLE_MARKER in text:
        return True
    at = text.find("if(DEFINED EASEL_DIR)")
    if at < 0:
        return False
    open(cmake_path, "w", encoding="utf-8").write(text[:at] + BUNDLE_BLOCK + text[at:])
    return True


def collect_licenses(easel_dir, dest):
    """把用到的第三方许可证收齐 —— MIT / OFL 分发时都要求带上。"""
    os.makedirs(dest, exist_ok=True)
    n = 0
    lic = os.path.join(easel_dir, "LICENSE")
    if os.path.exists(lic):
        shutil.copy2(lic, os.path.join(dest, "Easel-LICENSE.txt"))
        n += 1
    vendor = os.path.join(easel_dir, "vendor")
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
    ap.add_argument("project", help="工程目录")
    ap.add_argument("--name", default=None, help="作品名，默认用目录名")
    ap.add_argument("--easel", default=None, help="Easel 源码目录，默认找 <工程>/../easel")
    ap.add_argument("--no-deps", action="store_true",
                    help="源码包里不含 Easel 与第三方依赖（默认含，才能重新编译）")
    ap.add_argument("--skip-build", action="store_true", help="跳过编译，用已有的 exe")
    args = ap.parse_args()

    proj = os.path.abspath(args.project)
    if not os.path.isfile(os.path.join(proj, "CMakeLists.txt")):
        raise SystemExit(f"[打包] {proj} 不像一个工程目录（没有 CMakeLists.txt）")
    name = args.name or os.path.basename(proj)
    easel = os.path.abspath(args.easel) if args.easel else os.path.join(proj, "..", "easel")
    easel = os.path.abspath(easel)
    has_easel = os.path.isfile(os.path.join(easel, "CMakeLists.txt"))

    dist = os.path.join(proj, "dist")
    build = os.path.join(proj, "build", "release")
    prog_dir = os.path.join(dist, f"{name}-程序")
    os.makedirs(dist, exist_ok=True)

    print("=== 1/5 Release 构建 ===")
    if not args.skip_build:
        cfg = ["cmake", "-S", proj, "-B", build, "-DCMAKE_BUILD_TYPE=Release"]
        if has_easel:
            cfg.append(f"-DEASEL_DIR={easel}")
        run(cfg)
        run(["cmake", "--build", build, "--config", "Release", "--parallel"])
    exe = find_exe(build)
    if not exe:
        raise SystemExit(f"[打包] 在 {build} 里没找到 app / app.exe")
    print(f"    exe: {exe}  {os.path.getsize(exe)/1e6:.1f} MB")

    print("=== 2/5 程序包 ===")
    if os.path.isdir(prog_dir):
        shutil.rmtree(prog_dir)
    os.makedirs(prog_dir)
    shutil.copy2(exe, prog_dir)
    for d in ("assets", "data"):
        s = os.path.join(proj, d)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(prog_dir, d), ignore=SKIP)
    nlic = collect_licenses(easel, os.path.join(prog_dir, "第三方许可证")) if has_easel else 0
    with open(os.path.join(prog_dir, "运行说明.txt"), "w", encoding="utf-8") as f:
        f.write(RUN_NOTE.format(name=name, exe=os.path.basename(exe)))
    prog_zip = os.path.join(dist, f"{name}-程序.zip")
    prog_size = zip_dir(prog_dir, prog_zip, f"{name}-程序")
    print(f"    {os.path.relpath(prog_zip, proj)}  {prog_size/1e6:.1f} MB（含 {nlic} 份许可证）")

    print("=== 3/5 源码包 ===")
    stage = os.path.join(dist, ".源码暂存")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    root = os.path.join(stage, name)
    os.makedirs(root)
    missing = []
    for item in SOURCE_ITEMS:
        s = os.path.join(proj, item)
        if not os.path.exists(s):
            missing.append(item)
            continue
        d = os.path.join(root, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, ignore=SKIP)
        else:
            shutil.copy2(s, d)
    if not args.no_deps and has_easel:
        eroot = os.path.join(root, "easel")
        os.makedirs(eroot)
        for item in ("CMakeLists.txt", "CMakePresets.json", "LICENSE", "README.md",
                     "include", "src", "assets", "scripts", "docs", "vendor"):
            s = os.path.join(easel, item)
            if not os.path.exists(s):
                continue
            d = os.path.join(eroot, item)
            if os.path.isdir(s):
                shutil.copytree(s, d, ignore=SKIP)
            else:
                shutil.copy2(s, d)
    bundled = ensure_bundle_block(os.path.join(root, "CMakeLists.txt")) \
        if (not args.no_deps and has_easel) else True
    src_zip = os.path.join(dist, f"{name}-源码.zip")
    src_size = zip_dir(stage, src_zip, "")
    shutil.rmtree(stage)
    print(f"    {os.path.relpath(src_zip, proj)}  {src_size/1e6:.1f} MB"
          f"{'（含 Easel 与依赖，可重新编译）' if not args.no_deps and has_easel else ''}")
    if missing:
        print(f"    （工程里没有这些，跳过：{', '.join(missing)}）")

    print("=== 4/5 自检 ===")
    lines = []
    def check(ok, text):
        lines.append(("  [√] " if ok else "  [×] ") + text)
        return ok

    with zipfile.ZipFile(src_zip) as z:
        names = z.namelist()
    check(not any("/build/" in n or n.startswith("build/") for n in names), "源码包里没有 build/")
    check(any(n.endswith("src/solver.cpp") for n in names), "源码包里有 src/solver.cpp")
    check(any(n.endswith("src/easel_core.h") for n in names),
          "源码包里有 src/easel_core.h（一条 g++ 就能编算法）")
    check(bundled, "顶层 CMakeLists 会自己发现包里的 easel/（不用加 -DEASEL_DIR）")
    check(any("/tests/" in n for n in names), "源码包里有 tests/")
    check(any("/data/" in n for n in names), "源码包里有 data/")
    total = prog_size + src_size
    check(total < 500e6, f"程序包 + 源码包 = {total/1e6:.0f} MB，离 500 MB 上限还有 "
                         f"{(500e6-total)/1e6:.0f} MB（视频另算）")
    check(nlic > 0, f"带了 {nlic} 份第三方许可证")
    import platform
    win = platform.system() == "Windows"
    check(win, f"在 {platform.system()} 上打的包"
               f"{'' if win else ' —— 要在 Windows 上运行就要在 Windows 上重打一次'}")

    print("=== 5/5 写检查清单 ===")
    report = ["发布包自检 —— " + name, ""] + lines
    txt = "\n".join(report) + "\n"
    with open(os.path.join(dist, "检查清单.txt"), "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt)
    print(f"全部在 {dist}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
