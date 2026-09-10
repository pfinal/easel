#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 include/easel/core.h + nlohmann/json.hpp 合成一个 easel.hpp（stb 风格）。

学生的 solver.cpp 只 #include 这一个文件，然后：

    g++ -std=c++17 -DEASEL_STANDALONE solver.cpp && ./a.out

用法：
    python3 scripts/amalgamate.py                     # 自动找 json.hpp（找不到就下载）
    python3 scripts/amalgamate.py --json path/json.hpp
    python3 scripts/amalgamate.py --check             # 只检查 dist/ 是否已是最新
"""
import argparse
import hashlib
import os
import re
import sys
import urllib.request

# Windows 上标准输出默认 cp1252，打中文会炸；统一成 UTF-8（Python 3.7+）
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_VERSION = "3.12.0"
JSON_URL = f"https://github.com/nlohmann/json/releases/download/v{JSON_VERSION}/json.hpp"

SEARCH = [
    os.path.join(ROOT, "build", "_deps", "nlohmann_json-src", "single_include", "nlohmann", "json.hpp"),
    os.path.join(ROOT, "build", "default", "_deps", "nlohmann_json-src", "single_include", "nlohmann", "json.hpp"),
    os.path.join(ROOT, ".cache", "json.hpp"),
]


def find_json(explicit):
    if explicit:
        return explicit
    if os.environ.get("EASEL_JSON_HPP"):
        return os.environ["EASEL_JSON_HPP"]
    for p in SEARCH:
        if os.path.exists(p):
            return p
    # 都没有就下载一份缓存起来
    cache = os.path.join(ROOT, ".cache", "json.hpp")
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    print(f"[amalgamate] 本地没有 json.hpp，正在下载 {JSON_URL}")
    urllib.request.urlretrieve(JSON_URL, cache)
    return cache


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def easel_version(core_src):
    m = re.search(r'#define\s+EASEL_VERSION\s+"([^"]+)"', core_src)
    return m.group(1) if m else "0.0.0"


def build(core_path, json_path):
    core = read(core_path)
    js = read(json_path)
    ver = easel_version(core)
    jver = "?"
    m = re.search(r"version (\d+\.\d+\.\d+)", js[:4000])
    if m:
        jver = m.group(1)

    banner = f"""// ============================================================================
//  easel.hpp — Easel {ver} 单文件版（自动生成，请勿手改）
//
//  由 scripts/amalgamate.py 从 include/easel/core.h + nlohmann/json {jver} 合成。
//  改动请改仓库里的 include/easel/core.h，然后重新生成。
//
//  用法：
//      #include "easel.hpp"
//      g++ -std=c++17 -DEASEL_STANDALONE solver.cpp && ./a.out
//
//  Easel: MIT · 代码酷 daimaku.net
//  nlohmann/json: MIT · https://github.com/nlohmann/json
// ============================================================================
#ifndef EASEL_CORE_SINGLE_HEADER
#define EASEL_CORE_SINGLE_HEADER
#define EASEL_AMALGAMATED 1

// ---------------------------------------------------------------------------
// 以下 {len(js.splitlines())} 行是 nlohmann/json {jver}（MIT）原文，可以整段折叠不看。
// ---------------------------------------------------------------------------
"""
    tail = "\n#endif  // EASEL_CORE_SINGLE_HEADER\n"
    body = f"""
// ---------------------------------------------------------------------------
// 以下是 Easel core（include/easel/core.h）
// ---------------------------------------------------------------------------
"""
    return banner + js + body + core + tail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", help="nlohmann json.hpp 单文件路径")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "easel.hpp"))
    ap.add_argument("--also", action="append", default=[os.path.join(ROOT, "template", "src", "easel.hpp")],
                    help="同时写一份到这里（默认同步模板工程）")
    ap.add_argument("--check", action="store_true", help="只检查是否最新，不写文件")
    args = ap.parse_args()

    core_path = os.path.join(ROOT, "include", "easel", "core.h")
    json_path = find_json(args.json)
    if not os.path.exists(json_path):
        print(f"[amalgamate] 找不到 json.hpp: {json_path}", file=sys.stderr)
        return 2

    out = build(core_path, json_path)
    digest = hashlib.sha256(out.encode("utf-8")).hexdigest()[:12]

    if args.check:
        stale = []
        for p in [args.out] + args.also:
            if not os.path.exists(p) or read(p) != out:
                stale.append(p)
        if stale:
            print("[amalgamate] 这些文件不是最新的，请运行 python3 scripts/amalgamate.py：")
            for p in stale:
                print("   ", os.path.relpath(p, ROOT))
            return 1
        print(f"[amalgamate] dist/ 与 template/ 都是最新的（{digest}）")
        return 0

    for p in [args.out] + args.also:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write(out)
        print(f"[amalgamate] 写出 {os.path.relpath(p, ROOT)}  {len(out.splitlines())} 行  sha {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
