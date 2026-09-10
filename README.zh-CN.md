# Easel

[English](README.md)

*Easel 是一个 C++ 创意编程库：像 Processing 那样写画布、面板、画笔和声音；按 F12 打开内建的调试台，算法过程可以逐帧回放。*

![Creative pack example](docs/screenshot-creative.png)

## 亮点

**调试台是内置的。** 按 F12 打开：六个页面 —— Log、Trace（`EASEL_TRACE` 会自动画成折线图）、State（`dbg()` 变成实时表格）、Canvas 诊断（当前帧有多少图元、有多少在画面外）、Export case、Self-check。界面里看到的问题可以从 "Cases" 页面导出为 case 文件，用同一个种子在命令行复现。

**只算一次，逐帧回放。** `Timeline<T>` 保存整段计算好的序列，`App::transport()` 附加一条回放条：播放、暂停、单步、拖动进度、调整速度。

**项目是自包含的。** "Export source" 会生成一个包含所有依赖源码的目录；在一台什么都没装、也没有网络的机器上，一条 `cmake` 命令就能构建出完整的界面程序。

## 你能得到什么

| Category | Contents |
|---|---|
| Canvas | 世界坐标、相机缩放和平移、图元（line / rect / circle / triangle / polygon / bezier / 自定义形状）、变换栈 `push/translate/rotate/scale/pop` |
| Paint layer | `Layer`：draw、stamp、clear —— 在每帧重绘之外持久存在 |
| Images and sprites | `loadTexture()`、`c.image()`，用一个 `Rect` 从序列帧图里取出一帧 |
| Text | `c.text()`，中文字体会自动从系统字体中定位 |
| Panel widgets | slider、toggle、button、stat card、line chart；随时可以 `#include <imgui.h>` |
| Sound | wav / mp3 / flac 播放、音量、响度、频谱 |
| Noise and random | `noise()` / `noiseSeed()`、`rng()`，本次运行的种子会在启动时打印，可用 --seed N 复现 |
| Theme | 五套预设：Forest / Ocean / Ember / Paper / Slate |

## 最小示例

这就是空白工程 `src/app.cpp` 的全部内容（Workbench →「新建工程」→ 骨架：空白）：

```cpp
#include <easel/easel.h>
using namespace easel;

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("MySketch");
    app.onDraw([](Canvas& c) {
        c.text({0, 0}, "Hello, Easel", Align::Center);
    });
    return app.run();
}
```

运行后画布正中间会有一行 "Hello, Easel"，别的什么都没有。
想看一个稍微复杂一点、可交互的例子（一圈点 + 拖拽滑块），看
[`examples/hello`](examples/hello) —— 新建工程对话框的「骨架」下拉里也有，叫「示例：hello」。

![Empty project](docs/screenshot-hello.png)

## 快速开始

### macOS

```bash
git clone https://github.com/pfinal/easel.git && cd easel
cmake --preset default && cmake --build --preset default
```

`./build/default/easel` 会打开 Workbench："New Project" -> "Build & Run"。这需要 Xcode 命令行工具（`xcode-select --install`）和 CMake。

发布包：下载 zip，双击 `Easel.app`；需要 Xcode 命令行工具和 CMake。

### Windows

有一个免安装工具箱，包含 gcc、cmake、ninja 和 Easel，由 `scripts/make_toolbox.py` 打包生成。解压后双击 `easel.bat`（Workbench 启动器）。该工具箱的官方构建会随第一个发布版本一起提供。系统要求 Windows 7 SP1 x64 及以上（Win10/11 同样适用）；Win7 尚未实测。详见 [`docs/windows-toolbox.md`](docs/windows-toolbox.md)（含 OpenGL/DX11 后端退路说明）。

## Workbench

New Project / Build & Run / Stop / Build exe / Export source。项目以独立进程运行；点击错误行会跳转到源码中对应的位置。空白工程只有一个文件 —— `src/app.cpp` —— 构建脚本放在 `.easel/` 里，单头库不拷贝，直接指到 Easel 自己的 `dist/`。「骨架」下拉还可以选算法骨架（数据文件、逐帧回放、收敛曲线）或者任意一个自带示例。作品名只能用英文（编译器对中文路径支持不好）。

每个按钮都有命令行等价物（`--build` / `--run` / `--package` / `--export`，还有无头的 `--new`）；加 `--json` 拿到机器可读的结果，例如 `easel --build ~/projects/Demo --json`。

![Workbench](docs/screenshot-workbench.png)

## 调试台

按 F12 打开：六个页面 —— Log（级别 / file:line / 帧号 / 重复折叠）、Trace（`EASEL_TRACE` 自动变成曲线）、State（`dbg()` 变成实时表格）、Canvas（当前帧有多少图元、有多少在画面外、当前世界范围）、Cases（导出一个调试 case、复现命令）、Self-check（后端 / GPU / DPI / 字体 / 种子 / 编译器）。

![Debug console](docs/screenshot-debug.png)

## 文档

- [`docs/reference.md`](docs/reference.md) —— 完整 API 参考（签名/参数/返回值），中文。
- [`docs/guide.md`](docs/guide.md) —— 使用指南：Scratch / Processing / openFrameworks 对照表、项目结构惯例、设计原因，中文。
- [`docs/cheatsheet.md`](docs/cheatsheet.md) —— 一页速查表，中文。
- [`examples/`](examples/) —— `hello`（一圈点 + 滑块，最小的可交互示例）、`sort`（排序可视化）、`creative`（万花筒 / 轨迹 / 噪声地形 / 序列帧动画 / 声音）。

## 从源码构建

所有依赖都通过 `FetchContent` 静态链接：GLFW 3.4、Dear ImGui v1.92.9b-docking、ImPlot v1.0、nativefiledialog-extended v1.3.0、nlohmann/json v3.12.0、doctest v2.4.12、stb（commit 2c980bb）、ImGuiColorTextEdit（commit a20e449）、miniaudio 0.11.25。

```bash
python3 scripts/vendor.py       # 把九个依赖下载到 vendor/，之后完全离线
cmake --preset default          # 或 debug（ASan）/ dx11（Windows DirectX 11 后端）
cmake --build --preset default
ctest --preset default
```

## 目录结构

| Directory | Contents |
|---|---|
| `include/` | 公开头文件：`core.h`（无 GUI，零链接依赖）、`app.h`、`canvas.h`、`camera.h`、`timeline.h`、`ui.h`、`audio.h`、`theme.h`、`file.h` |
| `src/` | 库的实现 |
| `workbench/` | Workbench：新建项目 / 构建 / 运行 / 停止 / 构建 exe / 导出源码 |
| `template/` | 算法骨架起始项目（数据文件、回放、收敛曲线） |
| `template-hello/` | 空白项目模板（一个文件：`src/app.cpp`） |
| `examples/` | `hello`、`sort`、`creative` 示例 |
| `docs/` | 参考手册、速查表、截图 |
| `scripts/` | `vendor.py`（离线依赖）、`make_toolbox.py`（Windows 工具箱）、`package.py`（发布打包） |
| `tests/` | 单元测试（doctest） |

## 许可证

MIT，见 [`LICENSE`](LICENSE)。第三方依赖的许可证包含在发布包中。

---

代码酷 daimaku.net 出品
