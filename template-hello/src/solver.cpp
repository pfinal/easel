// ============================================================================
//  solver.cpp —— 逻辑写在这里
//
//  逻辑写在这里：数据、状态、每帧要算的东西。它同时是一个能单独编译运行的程序
//  （g++ -std=c++17 -DEASEL_STANDALONE -I.easel src/solver.cpp）——做算法类作品时，
//  这条命令让它像普通命令行程序一样在终端里跑；做纯视觉作品时不用理会这一点。
//  界面（app.cpp）会把它整个 #include 进去，所以两边跑的是同一份代码 ——
//  断点打在这个文件里，命令行和界面都能停下来。
//
//      g++ -std=c++17 -DEASEL_STANDALONE -I.easel src/solver.cpp -o solver
// ============================================================================
#include "easel_core.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace easel;

// ---- 数据：一个全局变量就够，不用写 class ---------------------------------
struct State {
    int    count = 8;      // 画几个点
    double radius = 20.0;  // 撒多开

    std::vector<Vec2> points;   // 算出来的结果
};
State S;

// ---- 算法：换成你自己的 ----------------------------------------------------
// 现在这份只是让工程一建出来就有东西看：把 count 个点均匀摆成一圈。
void solve() {
    S.points.clear();
    for (int i = 0; i < S.count; ++i) {
        double a = 2 * 3.14159265358979 * i / S.count;
        S.points.push_back({std::cos(a) * S.radius, std::sin(a) * S.radius});
    }
    EASEL_LOG("算完了：%d 个点", (int)S.points.size());
}

// ---- 命令行入口 ------------------------------------------------------------
// 只有单独编 solver.cpp 时才会编进来；app.cpp #include 它的时候这段自动消失。
#ifdef EASEL_STANDALONE
int main(int argc, char** argv) {
    cli::parse(argc, argv);     // --seed 123 换随机种子，--count 12 之类自己取
    if (cli::args().has("count")) S.count = (int)cli::args().num("count", 8);

    solve();
    for (const Vec2& p : S.points) std::cout << p.x << ", " << p.y << "\n";
    return 0;
}
#endif
