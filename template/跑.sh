#!/usr/bin/env bash
# 编译 + 运行（Mac / Linux）。Windows 上用同目录的 跑.bat。
# 多给的参数会转交给 app，例如：
#     ./跑.sh --seed 7          换个随机种子
#     ./跑.sh --debug           启动就打开 F12 调试台
set -e
cd "$(dirname "$0")"

# 旁边有 easel 源码就用本地那份，没有就走 FetchContent（要能上网）
EXTRA=()
[ -f ../easel/CMakeLists.txt ] && EXTRA=(-DEASEL_DIR=../easel)

if [ ! -f build/default/CMakeCache.txt ]; then
  echo "[1/2] 第一次运行，先配置（要编 ImGui，几分钟）..."
  cmake --preset default "${EXTRA[@]}"
fi

echo "[2/2] 编译..."
cmake --build --preset default

echo
./build/default/bin/app --open data/example.json --solve "$@"
