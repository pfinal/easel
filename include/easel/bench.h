// Easel — bench.h
// bench::run 和 Stopwatch 就住在 core.h 里（CLI 下也要能测时间，而 core.h 是零依赖的）。
// 这个文件只是为了让 #include <easel/bench.h> 也能用。
#ifndef EASEL_BENCH_H
#define EASEL_BENCH_H
#include <easel/core.h>
#endif
