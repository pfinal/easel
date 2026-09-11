# 发版流程（维护者文档）

给要出正式 Release 的人看的。学生/普通用户不需要读这份，直接去
[Releases](https://github.com/pfinal/easel/releases) 下包就行。

## 整体流程

1. 改版本号（下面「要改哪几处」）。
2. 跑 `python3 scripts/amalgamate.py`，把 `dist/easel.hpp` 重新生成一遍
   （`.github/workflows/ci.yml` 的 `bare-compile` job 会用
   `scripts/amalgamate.py --check` 校验 `dist/` 是不是最新的，version 字符串
   编进了合成产物的头部注释和内容里，版本号改了不跑这一步 CI 会红）。
3. 提交：

   ```bash
   git add CMakeLists.txt include/easel/core.h dist/
   git commit -m "chore: bump version to 0.1.2"
   git push
   ```

4. 打 tag、推上去：

   ```bash
   git tag v0.1.2
   git push --tags
   ```

   `.github/workflows/release.yml` 会在 tag 推上去之后自动跑：并行编 Windows
   绿色工具箱 zip（`windows` job）、macOS `.app` zip（`macos` job）、Linux
   tar.gz（`linux` job），三个都成功之后 `release` job 把它们挂到一个**草稿**
   Release 上（`draft: true`，不会直接公开）。

5. 人工检查：去 Actions 页面等四个 job 跑完，去 Releases 页面找到那个新草稿：
   - 三份产物都下载下来，本机实际解压、打开，确认能跑起来（Windows 双击
     `easel.exe`，macOS 把 `Easel.app` 拖进「应用程序」双击，Linux 解压后跑
     `./bin/easel`）。
   - 草稿的 body 是自动生成的，核对一下版本号、文件名对不对。
   - 没问题了，在 Releases 页面点这个草稿 → **Publish release**。

   出了问题（包里缺东西、版本号错了……）：删掉这个草稿 Release（不影响已经
   打的 tag），改好之后要么改完重新推一个新 tag（比如 `v0.1.3`），要么删掉
   本地和远端的旧 tag 后原地重打：

   ```bash
   git push --delete origin v0.1.2
   git tag --delete v0.1.2
   # 改完，重来第 4 步
   ```

调试这条流水线本身（release.yml 有没有写对），不用真的打 tag——
`workflow_dispatch` 也能手动跑（Actions 页面 → Release → Run workflow）。手动
跑时如果不填 `tag` 输入框，草稿 Release 会用当前分支名当 tag（不是真实版本
号），纯粹为了验证流程能不能走通，跑完记得把这个调试用的草稿 Release 删掉。

## 版本号要改哪几处

Easel 的版本号存了两份，都要跟着改，两边不一致的话 `scripts/make_toolbox.py`
/ `scripts/make_bundle_mac.py` / `scripts/make_tarball_linux.py` 产出的包名和
`easel-prebuilt.json` 里的 `version` 字段用的是 `include/easel/core.h` 那份，
跟 `CMakeLists.txt` 那份不会自动同步：

1. `CMakeLists.txt` 顶部的 `project(easel VERSION 0.1.1 LANGUAGES CXX)`。
2. `include/easel/core.h` 里的四个宏：

   ```cpp
   #define EASEL_VERSION       "0.1.2"
   #define EASEL_VERSION_MAJOR 0
   #define EASEL_VERSION_MINOR 1
   #define EASEL_VERSION_PATCH 2
   ```

   `scripts/make_toolbox.py` / `scripts/make_bundle_mac.py` 的 `easel_version()`
   是从这个文件里用正则 `#define\s+EASEL_VERSION\s+"([0-9.]+)"` 抠出来的，格式
   （引号、宏名、空格）不能改。

3. 改完两处之后跑 `python3 scripts/amalgamate.py`——版本号也会被编进
   `dist/easel.hpp`。

release.yml 里 Release 草稿的标题/tag 用的是 **git tag**（比如 `v0.1.2`），跟
上面这两处的版本号是分开的两件事：tag 决定这次 Release 叫什么、挂在哪个
commit 上；`core.h` 里的版本号决定产物 zip 文件名里的版本号（`Easel-0.1.2-
windows-x64.zip` 这种）。打 tag 前先把 `core.h` 的版本号改成一致，不然会出现
「tag 叫 v0.1.2，包名却是 Easel-0.1.1-...」这种对不上的情况。

## 限制：Windows 包里的预编译产物只能由 CI 产

`release.yml` 的 `windows` job 在 `windows-latest` runner 上用 w64devkit
（MinGW-w64 的 gcc/g++）+ Ninja 把 Easel 编一遍、`cmake --install` 装出
Windows/MinGW 的预编译包（`.a` 静态库 + `easel.exe`），再用
`scripts/make_toolbox.py --prebuilt-dir` 塞进绿色工具箱 zip 里。

这一步**必须在真正的 Windows 机器（或者 CI 的 Windows runner）上跑**——本机
是 Mac，Mac 上的 clang 编不出 MinGW/Windows 能跑的 `.exe`/`.a`（`.a` 里的目
标文件格式、调用约定、符号修饰都跟 Windows 的不是一回事，交叉编译需要一整
套 MinGW 交叉工具链，这仓库没有配这个）。`scripts/make_toolbox.py` 本身可以
在 Mac 上跑（不给 `--prebuilt-dir` 就行），但那样组出来的工具箱里没有预编译
包，用户解压后第一次点 `easel.bat` 要现场编三分钟——这是本机唯一能验的路径
（`python3 scripts/make_toolbox.py`，不带 `--prebuilt-dir`），完整验证（带预
编译包那条路）只能靠 CI。

同理，`scripts/make_bundle_mac.py` 产的 `.app` 只能在 macOS 上编——脚本开头
就检查了 `sys.platform != "darwin"` 直接退出。CI 里 `macos` job 跑在
`macos-latest`（Apple Silicon）上，但打包脚本传了
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 编通用二进制（universal binary），
所以这一台机器编出来的 `Easel.app` 同时支持 Apple Silicon 和 Intel，不需要
额外的 Intel runner 矩阵项。

## 限制：Linux 包必须在 `ubuntu-22.04` 上编，不是 `ubuntu-latest`

`release.yml` 的 `linux` job 固定 `runs-on: ubuntu-22.04`（`ci.yml` 里天天跑
的 `build` job 用的是 `ubuntu-latest`，那个不用动——它只验证「代码在最新
Ubuntu 上编得过」，不产发布物）。发布包链接的是打包那台机器上的 glibc；
`ubuntu-latest` 目前是 24.04，链出来的二进制在装着更旧 glibc 的机器上会直接
启动不了（`GLIBC_2.3x not found`）。`ubuntu-22.04` 的 glibc 明显更旧，兼容
面更广，所以固定用它，将来 GitHub 把 `ubuntu-22.04` 这个标签下线之前不用改。

跟 `scripts/make_toolbox.py`（Windows）一样，`scripts/make_tarball_linux.py`
自己不碰编译器——它只管组装：拷可执行文件、拷源码树、拷 `cmake --install` 装
出来的预编译包、收集许可证、写 `README.txt`、打 `tar.gz`。真正的配置/编译/
安装是 `linux` job 自己用 `cmake` 做的（跟 Windows job 的写法一模一样），组装
脚本用 `--exe` / `--prebuilt-dir` 两个参数认这两份产物。这样一来，脚本里跟
平台无关的那部分（许可证收集、README 文案、tar.gz 打包）在任何装了 Python 3
的机器上都能单独验证——不用每次改一行 README 文案就重新在 Linux 上编一遍
Easel；但完整链路（编译 + 装预编译 + 组装 + 解压跑起来）仍然要在 Linux 上
（真机、Linux 容器、或 `ubuntu-22.04` CI runner）跑一遍才算数，`ci.yml` 的
`linux-tarball-smoke` job 就是干这个的——参见脚本顶部的说明和该 job 里的断言
（解压到跟构建目录无关的路径、`--doctor` 里的 Easel 源码指向解压目录、
`--build` 命中预编译而不是源码编、`--export` 自检没有 `[×]`）。
