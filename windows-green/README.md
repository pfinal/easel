# Windows 绿色方案

一个 zip，解压就能写、能编、能调、能交。**不装任何东西，不要管理员权限，不写注册表，不用联网。**

「还原卡一重启全没了」「出不去外网」的机器，这是唯一走得通的路。

---

## 里面有什么

| | 是什么 | 大约 |
|---|---|---|
| `w64devkit\` | **gcc 14.1 + gdb + make**，完整的 MinGW-w64 工具链 | 76 MB |
| `cmake\` | 构建 GUI 工程要用，**3.31.6**（Kitware 支持 Win7 的最后一个系列） | 54 MB |
| `ninja\` | 构建器，快 | 0.3 MB |
| `easel\` | Easel 本体 + 模板工程 + **全部依赖的源码**（所以不用联网） | 45 MB |
| `Easel.bat` | **从这里开始**：新建工程 / 编译 / 运行 / 停止 / 生成 exe / 导出源码。第一次双击会先把工作台自己编出来（几分钟） | |
| `start.bat` | 把上面几样加进本次会话的 PATH，开一个命令行窗口（要手动敲命令时才用） | |

zip **151 MB**，解压后 **509 MB**。

> 解压路径别太深，`D:\daimaku` 这种就行 —— Windows 有 260 字符的路径长度限制，
> CMake 和 gcc 的目录本来就深。

**里面没有编辑器**，这是故意的：编译器只能有一个、必须有；编辑器是你的自由。
用什么写代码见下面那一节。

> 写代码用 VS Code（或任何编辑器），**编译、运行、停止、生成 exe、导出源码走工作台**
> （`Easel.bat`，D-29）。作品是工作台的子进程，所以改完界面代码点一下就能重跑。
> 作品里还留了一栏 F9 编辑器当兜底 —— 机器上真没有 VS Code 时能改一行算法。

## 里面**没有**什么

**编辑器。** 自己装一个喜欢的（VS Code、小熊猫C++、什么都行），我们只管编译。
理由：编译器必须有、而且只能有一个；编辑器是个人偏好，塞进包里只会多一个没人用的文件夹。
（Easel 界面里那一栏 F9 编辑器不算数：它只服务 GUI 阶段的「改一行、看一眼」，
D-27 里写明了不做断点、不做补全。还需要一个真正的编辑器。）

**Windows 7 SP1 x64 或更新（Win10/11 同样适用）。** 工具箱里的 CMake 是 3.31.6 ——
Kitware 停供 Win7 官方包之前的最后一个系列，专门为了这个下限选的（4.x 系列已经不出 Win7 包了）。
Win7 尚未实测。

> 解压用的是系统自带的 `tar.exe`（Win10 1803 起才有）；Win7 上没有这个命令，
> 得用第三方解压工具（7-Zip / WinRAR 之类）把 zip 解开，别用 Win7 自带的「压缩文件夹」——
> 单个大 zip 它经常处理不动。

除此之外机器上什么都不用装 —— 尤其是不用单独装 C++，编译器就在包里。

> 界面需要 OpenGL 3.2：装过显卡驱动就有；老机器只有「Microsoft 基本显示适配器」时（OpenGL 只有
> 1.1，窗口开不起来）用 DX11 后端（`cmake --preset dx11`，代码写了没实测），`app --doctor`
> 会把显卡和实际用的后端直接打出来，一句话定位。
> 显卡驱动不用操心：装完 Windows、Windows Update 跑过一遍，厂商驱动和 OpenGL 3.2 就都有了。
> 只有几种边角情况会掉回「Microsoft 基本显示适配器」——
> 纯净镜像 + 断网 + Windows Update 被禁；远程桌面里跑；虚拟机没开 3D 加速；
> 2010 年以前的核显（Win7 老机器尤其容易碰上，见上面 DX11 退路）。
> 真碰不上驱动也可以往 exe 旁边放一份 Mesa 的软件渲染 `opengl32.dll`。

---

## 怎么做出这个 zip（做一次就行）

**在 Mac 上就能做**（不需要 Windows 机器）：

```bash
python3 scripts/vendor.py         # 1. 九个依赖抓到 vendor/（17 MB）
python3 scripts/make_toolbox.py   # 2. 下三个组件、组装、打包
```

产出 `windows-green/代码酷C++工具箱.zip`。之后每台机器只要这一个文件。

三个组件（w64devkit / CMake / Ninja）都是**纯 zip**，版本在 `scripts/make_toolbox.py`
的 `COMPONENTS` 里锁死，所以每次组出来的包一模一样。要升级就改那里。
下载会缓存在 `.cache/`，第二次组装不用重下。

## 怎么用

1. 解压到任意目录（`D:\代码酷` 之类，路径别带空格更省事）
2. 双击 `start.bat`
3. 第一次把模板拷成自己的工程（工程名只能用英文，编译器对中文路径支持不好）：
   ```
   xcopy /E /I easel\template MySketch
   cd MySketch
   cmake --preset mingw -DEASEL_DIR=..\easel
   cmake --build --preset mingw
   build\mingw\bin\app.exe --open data\example.json --solve
   ```

---

## 用什么写代码

**Easel 不绑定任何编辑器。** 工具箱只管编译，写代码用什么随便挑：

| | 怎么来 | 适合 |
|---|---|---|
| **VS Code** | 官网下，用户级安装不要管理员；装 C/C++ 和 CMake Tools 两个扩展 | 全程。模板里的四个 F5 配置就是给它准备的 |
| **小熊猫C++**（Dev-C++ 那一系） | 官方便携版，解压即用，中文界面 | 写算法那两周。很多人已经习惯用它 |
| 记事本 / 任何编辑器 | — | 都行，编译走命令行 |

模板里 `.vscode\launch.json` 的四个 F5 配置（Solver / Tests / App / 复现用例），
Windows 分支已经按这个工具箱配好了：gdb + `build\mingw`，装完 VS Code 直接能按 F5。

用小熊猫的话，选 **NoCompiler 便携版（9.5 MB）** 最省事——
在它的「编译器配置」里指向工具箱里的 `w64devkit\bin`，全机器只有一份 gcc。

### 算法阶段（前两周）：单文件，什么编辑器都行

`src\solver.cpp` 是一个**能自己编译运行的完整程序**（D-23）：

```
g++ -std=c++17 -DEASEL_STANDALONE src\solver.cpp -o solver.exe
solver.exe data\example.json
```

一条命令，不需要 CMake、不需要 Easel 的任何东西。
在小熊猫里就是「编译器选项里加 `-std=c++17 -DEASEL_STANDALONE`，然后 F9 / F10 / F5」——
断点、单步、看变量，和普通 C++ 程序一模一样。

### 界面阶段（9/22 起）：走 CMake

```
cmake --build --preset mingw          （改完代码敲这一条）
build\mingw\bin\app.exe --open data\example.json --solve
```

嫌麻烦就双击模板里的 `跑.bat`，它把这两条合成一条。

**这一段本来就不该打断点** —— `onDraw` 里停下来窗口会冻住。该看的是 Easel 的
**F12 调试台**：日志、追踪折线、状态表、画布诊断、导出用例、环境自检。
换句话说，GUI 阶段的「调试器」是 Easel 自己，不是 IDE。

## 三条路各管一段，不重叠

| 阶段 | 怎么编 | 出问题怎么查 |
|---|---|---|
| 算法（9/9–9/21） | 一条 `g++`，或编辑器里按编译键 | 编辑器的断点单步（VS Code 配置 1，小熊猫 F5） |
| 界面（9/22–10/5） | `cmake --build --preset mingw` 或双击 `跑.bat` | **Easel 的 F12 调试台** |
| 定位到具体某行 | — | 导出用例 → 回命令行 → 在单文件里下断点 |

（「用什么写」这一列没了 —— 三段都是「随便」。）

最后一行是整个设计的目的：**GUI 里发现问题，能带回那个熟悉的单文件里去查。**

---

## 状态

zip **已经组出来了**（`windows-green/代码酷C++工具箱.zip`，151 MB），内容也核对过：
g++ / gdb / cmake / ninja / Easel / 模板 / 依赖源码，17139 个文件都在。

但**一次都没在 Windows 上跑过**（这边还没有 Windows 机器），9/21 检查点 #1 实测。
已知的风险：

- MinGW 编 nfd（系统文件对话框）——最不确定的一块。真编不过就先把文件对话框停掉，
  用 `--open` 参数指定文件，功能不受影响
- `RtlCaptureStackBackTrace`（崩溃调用栈）在 MinGW 头文件里的声明
- 机器上 `start.bat` 拼出来的 PATH 对不对（三个目录：`w64devkit\bin`、`cmake\bin`、`ninja`）
- 中文文件名（模板工程里的 `跑.bat`）在 Windows 自带解压里会不会乱码 —— 工具箱自己的
  入口脚本（`start.bat` / `Easel.bat` / `export-prebuilt.bat`）现在都是纯 ASCII 文件名，
  不受影响（D-36）
