#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Easel 的 ABI 指纹 —— 判断「预编译包能不能用」的唯一算法，只此一份实现。

背景：`easel.exe`（工具，workbench/main.cpp）和 `prebuilt/`（用户工程要链接的
libeasel.a）是两回事。改一行工作台 UI 不该让预编译包被判成「过期」——只有会
影响 libeasel.a 这份 ABI 的东西变了，才该让预编译包失效。

「会影响 ABI 的东西」= 库的公开头文件 + 库自身的实现源码 + 根 CMakeLists.txt
（依赖版本、编译选项都在这一份文件里，它变了通常意味着 ABI 也变了，宁可保守）
+ 编译器身份（不同编译器/版本编出来的 .a 不能混用）。**不包含** workbench/ ——
那是工具本身的 UI，不影响用户工程链接的库。

两处要用到同一个结果：
  1. CMake 配置期（根 CMakeLists.txt）把 abi 写进 easel-prebuilt.json；
  2. scripts/make_toolbox.py / make_bundle_mac.py 把 abi 写进源码树那份 VERSION.json。
CMake 没法直接跑这份算法（file(SHA256 ...) 拼接顺序、GLOB 排序都要跟 Python 这边
一个字节都不差，两套实现迟早会漂移），所以 CMakeLists.txt 用
`execute_process(COMMAND ${Python3_EXECUTABLE} scripts/abi.py ...)` 调这个脚本，
而不是自己用 file(SHA256 ...) 再算一遍。**全仓库只有这一处实现。**

用法：
    python3 scripts/abi.py                                   # 源码树当前状态的 abi（编译器信息留空）
    python3 scripts/abi.py --compiler-id AppleClang --compiler-version 17.0.0
    python3 scripts/abi.py --repo-root /path/to/easel
"""
import argparse
import hashlib
import os
import sys


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _collect_files(repo_root):
    """收集参与 ABI 指纹的文件，排序后固定顺序（相对路径，用 / 分隔，跟平台无关）。

    - include/easel/*.h   公开头文件（用户工程 #include 的就是这些）
    - src/*.cpp, src/*.h  库的实现（不含 workbench/ —— 那是工具的 UI，不影响 ABI）
    """
    files = []

    include_dir = os.path.join(repo_root, "include", "easel")
    if os.path.isdir(include_dir):
        for name in os.listdir(include_dir):
            if name.endswith(".h"):
                files.append(os.path.join("include", "easel", name))

    src_dir = os.path.join(repo_root, "src")
    if os.path.isdir(src_dir):
        for name in os.listdir(src_dir):
            if name.endswith(".cpp") or name.endswith(".h"):
                files.append(os.path.join("src", name))

    files.sort()
    return files


def compute_abi(repo_root, compiler_id="", compiler_version="", cxx_standard="17"):
    """返回 12 个十六进制字符的 ABI 指纹。

    输入（按固定顺序拼接后整体再 sha256）：
      1. include/easel/*.h 每个文件的 sha256（按相对路径排序）
      2. src/*.cpp、src/*.h 每个文件的 sha256（按相对路径排序，不含 workbench/）
      3. 根 CMakeLists.txt 整个文件的 sha256（依赖 GIT_TAG、编译选项都在这一份里，
         它变了通常意味着 ABI 也变了，宁可保守，不逐条摘取 GIT_TAG）
      4. C++ 标准 + 编译器 id/version（不同编译器编出来的 .a 不能混用）

    repo_root 得是 Easel 的源码树根目录（有 CMakeLists.txt、include/、src/ 那一层）。
    """
    parts = []

    for rel in _collect_files(repo_root):
        parts.append(_sha256_file(os.path.join(repo_root, rel)))

    cmakelists = os.path.join(repo_root, "CMakeLists.txt")
    if os.path.isfile(cmakelists):
        parts.append(_sha256_file(cmakelists))

    parts.append(f"cxx_standard={cxx_standard}")
    parts.append(f"compiler_id={compiler_id}")
    parts.append(f"compiler_version={compiler_version}")

    combined = hashlib.sha256("\n".join(parts).encode("utf-8")).hexdigest()
    return combined[:12]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo-root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     help="Easel 源码树根目录，默认是这个脚本所在目录的上一级")
    ap.add_argument("--compiler-id", default="", help="CMAKE_CXX_COMPILER_ID，比如 AppleClang / GNU / MSVC")
    ap.add_argument("--compiler-version", default="", help="CMAKE_CXX_COMPILER_VERSION")
    ap.add_argument("--cxx-standard", default="17", help="CMAKE_CXX_STANDARD，默认 17")
    args = ap.parse_args()

    print(compute_abi(args.repo_root, args.compiler_id, args.compiler_version, args.cxx_standard))
    return 0


if __name__ == "__main__":
    sys.exit(main())
