# 内置字体（可选）

Easel 找中文字体的顺序：

1. `Theme::fontPath`（你在代码里指定的）
2. 环境变量 `EASEL_FONT`
3. `assets/fonts/NotoSansSC-subset.otf`（这个目录，可选）
4. 系统字体
   - Windows：微软雅黑 → 等线 → 黑体 → 宋体
   - macOS：苹方 → 冬青黑 GB → 华文黑体 → 宋体 → Arial Unicode
   - Linux：Noto Sans CJK → 文泉驿微米黑 → …
5. 都没有 → ImGui 内置字体（中文会变方框，状态灯会变黄，`--doctor` 会说明）

Mac 和 Windows 上第 4 步基本不会落空，所以内置子集只是兜底，
或者用在「字体必须在每台机器上完全一致」的场合（录视频、交给评委）。

要生成它：

```bash
pip install fonttools brotli
python3 scripts/make_font_subset.py
```

生成的是 Noto Sans CJK SC 的子集，**SIL Open Font License 1.1**，
随程序分发时要把许可证文本一起带上。
