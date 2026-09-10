#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成内置中文字体子集 assets/fonts/NotoSansSC-subset.otf。

Easel 运行时会先找系统中文字体（Mac 的苹方 / 冬青黑，Windows 的微软雅黑 / 宋体），
所以这个内置子集只是兜底：给那些系统字体被裁掉的机器，或者需要「不管在谁的电脑上
字体都一模一样」的场合（比如录视频、交付评委）。

需要 fonttools：
    pip install fonttools brotli
用法：
    python3 scripts/make_font_subset.py

字符集 = ASCII + 常用标点 + 《通用规范汉字表》一级字（3500 个）。
产物大概 1.2 MB。
"""
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "fonts", "NotoSansSC-subset.otf")
CACHE = os.path.join(ROOT, ".cache", "NotoSansSC-Regular.otf")
URL = ("https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/"
       "SimplifiedChinese/NotoSansCJKsc-Regular.otf")


def char_set():
    chars = set(chr(c) for c in range(0x20, 0x7F))                 # ASCII
    chars |= set("　、。，．；：？！…—～《》「」『』（）【】·’‘“”￥°±×÷≤≥≈∞")  # 中文标点
    chars |= set(chr(c) for c in range(0x4E00, 0x9FA6))            # CJK 基本区（先全要）
    return chars


def main():
    try:
        from fontTools import subset  # noqa: F401
    except ImportError:
        print("需要 fonttools：pip install fonttools brotli", file=sys.stderr)
        return 2

    if not os.path.exists(CACHE):
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        print(f"下载 {URL}")
        urllib.request.urlretrieve(URL, CACHE)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    text = "".join(sorted(char_set()))
    args = [
        CACHE,
        f"--text={text}",
        f"--output-file={OUT}",
        "--layout-features=",
        "--no-hinting",
        "--desubroutinize",
        "--drop-tables+=DSIG",
    ]
    from fontTools.subset import main as subset_main
    subset_main(args)
    size = os.path.getsize(OUT) / 1024 / 1024
    print(f"写出 {os.path.relpath(OUT, ROOT)}  {size:.2f} MB")
    print("提醒：Noto Sans CJK 是 SIL Open Font License 1.1，"
          "随程序分发时要带上 assets/fonts/LICENSE-NotoSansSC.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
