# Easel

[中文](README.zh-CN.md)

*A Processing-style creative coding library for C++, with a built-in debug console and frame-by-frame playback.*

![Creative pack example](docs/screenshot-creative.png)

## Highlights

**The debug console is built in.** Press F12 to open it: six pages -- Log, Trace (`EASEL_TRACE` is automatically drawn as a line chart), State (`dbg()` as a live table), Canvas diagnostics (how many primitives are in this frame, how many are off-screen), Export case, and Self-check. A problem seen in the interface can be exported from the "Cases" page into a case file and reproduced from the same seed on the command line.

**Compute once, replay frame by frame.** `Timeline<T>` holds the entire computed sequence, and `App::transport()` attaches a playback bar: play, pause, step, drag the progress, adjust speed.

**The project is self-contained.** "Export source" generates a directory with the source of all dependencies; on a machine with nothing installed and no network access, a single `cmake` command builds the whole interface program.

## What you get

| Category | Contents |
|---|---|
| Canvas | world coordinates, camera zoom and pan, primitives (line / rect / circle / triangle / polygon / bezier / custom shapes), transform stack `push/translate/rotate/scale/pop` |
| Paint layer | `Layer`: draw, stamp, clear -- persists outside of the per-frame redraw |
| Images and sprites | `loadTexture()`, `c.image()`, take one frame out of a sprite sheet with a `Rect` |
| Text | `c.text()`, Chinese fonts are located automatically from the system fonts |
| Panel widgets | slider, toggle, button, stat card, line chart; `#include <imgui.h>` is available at any time |
| Sound | wav / mp3 / flac playback, volume, loudness, spectrum |
| Noise and random | `noise()` / `noiseSeed()`, `rng()`, the run's seed is printed at startup and can be replayed with --seed N |
| Theme | five presets: Forest / Ocean / Ember / Paper / Slate |

## Minimal example

```cpp
#include <easel/easel.h>
using namespace easel;

struct State {
    int    count = 8;
    double radius = 20.0;
    std::vector<Vec2> points;
};
State S;

void solve() {
    S.points.clear();
    for (int i = 0; i < S.count; ++i) {
        double a = 2 * 3.14159265358979 * i / S.count;
        S.points.push_back({std::cos(a) * S.radius, std::sin(a) * S.radius});
    }
}

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("Hello, Easel").size(1280, 800).theme(Theme::Forest());

    app.onStart([&app] {
        solve();
        app.camera().fit(Rect::bounding(S.points), 80);
    });

    app.onDraw([&](Canvas& c) {
        c.stroke(app.theme().accent, 2);
        c.polyline(S.points, true);
        c.fill(app.theme().accent);
        for (const Vec2& p : S.points) c.dot(p, 6);
        c.fill(app.theme().fg);
        c.text({0, 0}, "Hello, Easel", Align::Center);
    });

    app.onPanel([] {
        if (ui::section("Parameters")) {
            if (ui::slider("Points", &S.count, 3, 40)) solve();
            if (ui::slider("Radius", &S.radius, 5.0, 60.0)) solve();
        }
    });

    return app.run();
}
```

Running it shows a ring of points and one line of "Hello"; dragging a slider recomputes it.

![Empty project](docs/screenshot-hello.png)

## Getting started

### macOS

```bash
git clone https://github.com/pfinal/easel.git && cd easel
cmake --preset default && cmake --build --preset default
```

`./build/default/workbench` opens the Workbench: "New Project" -> "Build & Run". This requires the Xcode command line tools (`xcode-select --install`) and CMake.

### Windows

There is a no-install toolbox containing gcc, cmake, ninja, and Easel, assembled by `scripts/make_toolbox.py`. After extracting it, double-click `workbench.bat`. The toolbox's official build ships together with the first release.

## Workbench

New Project / Build & Run / Stop / Build exe / Export source. A project runs as a separate process; clicking an error line jumps to the corresponding location in the source. A new project has only three files -- `README.md`, `src/app.cpp`, `src/solver.cpp` -- the build scripts and the library live in `.easel/`.

![Workbench](docs/screenshot-workbench.png)

## Debug console

Press F12 to open it: six pages -- Log (level / file:line / frame number / repeat folding), Trace (`EASEL_TRACE` automatically becomes a curve), State (`dbg()` as a live table), Canvas (how many primitives this frame, how many are off-screen, the current world extent), Cases (export a debug case, reproduce command), Self-check (backend / GPU / DPI / fonts / seed / compiler).

![Debug console](docs/screenshot-debug.png)

## Documentation

- [`docs/reference.md`](docs/reference.md) -- the full reference manual, in Chinese, with mapping tables from Scratch / Processing / openFrameworks.
- [`docs/cheatsheet.md`](docs/cheatsheet.md) -- a one-page cheat sheet, in Chinese.
- [`examples/`](examples/) -- `sort` (sorting visualization), `creative` (kaleidoscope / trail / noise terrain / sprite animation / sound).

## Building from source

All dependencies are statically linked through `FetchContent`: GLFW 3.4, Dear ImGui v1.92.9b-docking, ImPlot v1.0, nativefiledialog-extended v1.3.0, nlohmann/json v3.12.0, doctest v2.4.12, stb (commit 2c980bb), ImGuiColorTextEdit (commit a20e449), miniaudio 0.11.25.

```bash
python3 scripts/vendor.py       # fetch the nine dependencies into vendor/, fully offline after that
cmake --preset default          # or debug (ASan) / dx11 (Windows DirectX 11 backend)
cmake --build --preset default
ctest --preset default
```

## Layout

| Directory | Contents |
|---|---|
| `include/` | public headers: `core.h` (no GUI, zero link dependencies), `app.h`, `canvas.h`, `camera.h`, `timeline.h`, `ui.h`, `audio.h`, `theme.h`, `file.h` |
| `src/` | library implementation |
| `workbench/` | Workbench: new project / build / run / stop / build exe / export source |
| `template/` | starter project with a playback skeleton |
| `template-hello/` | empty project template |
| `examples/` | the `sort` and `creative` examples |
| `docs/` | reference manual, cheat sheet, screenshots |
| `scripts/` | `vendor.py` (offline dependencies), `make_toolbox.py` (Windows toolbox), `package.py` (release package) |
| `windows-green/` | notes on the Windows no-install toolbox |
| `tests/` | unit tests (doctest) |

## License

MIT, see [`LICENSE`](LICENSE). Third-party dependency licenses are included with the release package.

---

Produced by daimaku.net
