// ============================================================================
//  solver.cpp —— 你唯一要写算法的文件
//
//  它是一个完整的、能自己编译运行的程序：
//      g++ -std=c++17 -DEASEL_STANDALONE src/solver.cpp -o solver && ./solver
//
//  另外两个入口用 #include 把它整个搬过去（unity include）：
//      src/app.cpp          #include "solver.cpp"   -> 图形界面
//      tests/test_solver.cpp #include "../src/solver.cpp" -> 对拍与回归
//
//  三条规矩：
//    1. 这个文件只 #include "easel_core.h" 和标准库。不要在这里 #include <imgui.h>。
//    2. 每个可执行文件恰好 #include 它一次。没有 .h、没有声明重复、没有链接顺序。
//    3. 断点打在这个文件里 —— 三个入口都能停下来。
// ============================================================================
#include "easel_core.h"

#include <iostream>
#include <string>
#include <vector>

using namespace easel;   // OI 习惯，随便用

// ============================================================================
//  1. 数据
//  加了 EASEL_JSON 的结构体就能存读文件、能被「导出调试用例」写进去。
// ============================================================================
struct Node {
    std::string name;
    Vec2        pos;      // 世界坐标。单位是什么由你定：米、格子、秒都行
};
EASEL_JSON(Node, name, pos)

struct Project {
    std::string        title = "未命名";
    double             unitScale = 1.0;       // 一个世界单位代表多少现实单位（画比例尺用；不需要就删掉）
    std::vector<Node>  nodes;
};
EASEL_JSON(Project, title, unitScale, nodes)

struct Params {
    int    iterations = 200;
    double alpha = 0.5;
};
EASEL_JSON(Params, iterations, alpha)

// 一帧 = 某个时刻的完整画面。solve() 一次算完，界面负责回放（D-09）。
struct Frame {
    std::vector<int> order;      // 当前的顺序 / 路线 / 解
    double           cost = 0;   // 当前的目标值
    int              focus = -1; // 想高亮的那个元素，没有就 -1
};
EASEL_JSON(Frame, order, cost, focus)

// 全局状态。一个就够，不需要写 class（D-07）。
struct State {
    Project            project;
    Params             params;
    std::vector<Frame> history;
};
State S;

// ============================================================================
//  2. 算法
//  ↓↓↓ 你的东西写在这里。↓↓↓
//
//  下面这份是**占位算法**，只为了让工程一建出来就能跑、能看：
//  「把点按某个顺序连起来，让总长度尽量短」。
//  你的题目多半不是这个 —— cost() 和 solve() 整段删掉重写就行，
//  只要保持「一次算完，返回一串 Frame」这个形状（D-09），界面那边一行都不用改。
// ============================================================================

// 目标函数：按 order 的顺序把点连起来，算出来的这个数就是「解有多好」。
double cost(const Project& p, const std::vector<int>& order) {
    double total = 0;
    for (size_t i = 0; i + 1 < order.size(); ++i)
        total += dist(p.nodes[order[i]].pos, p.nodes[order[i + 1]].pos);
    return total * p.unitScale;
}

// 一次算完，返回全过程。UI 只负责回放。
std::vector<Frame> solve(const Project& p, const Params& params) {
    std::vector<Frame> history;
    if (p.nodes.empty()) {
        EASEL_WARN("工程里一个点都没有，没什么可算的");
        return history;
    }

    std::vector<int> order(p.nodes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    rng().shuffle(order);                     // 先打乱，好让下面的「优化」有活干

    double best = cost(p, order);
    history.push_back(Frame{order, best, -1});

    for (int it = 0; it < params.iterations; ++it) {
        // ---- 随便换两个位置试试（把这段换成你的算法）----
        std::vector<int> cand = order;
        int              a = rng().i(0, (int)cand.size() - 1);
        int              b = rng().i(0, (int)cand.size() - 1);
        std::swap(cand[a], cand[b]);
        double c = cost(p, cand);

        if (c < best) {
            order = cand;
            best = c;
            history.push_back(Frame{order, best, a});
        }

        // 同一行代码：命令行里打成 CSV，App 里自动变折线
        EASEL_TRACE("目标值", best);
    }

    // 对拍式断言：命令行下立刻停住并打印调用栈，App 里弹红条但不崩
    EASEL_CHECK(history.back().order.size() == p.nodes.size(), "解的长度和点数对不上");
    EASEL_CHECK_NEAR(history.back().cost, cost(p, history.back().order), 1e-6);

    EASEL_LOG("算完了：%d 个点，%d 帧，最终目标值 %.2f", (int)p.nodes.size(), (int)history.size(),
              history.back().cost);
    // cout 也随便写：命令行在终端看，界面里在 F12 的日志窗看（Windows 上也一样）
    std::cout << "solve() 用的种子是 " << current_seed() << std::endl;
    return history;
}

// ============================================================================
//  3. 读写
// ============================================================================
bool loadProject(const std::string& path, Project* out) {
    json j = fs::loadJson(path);
    if (j.is_null()) return false;
    *out = j.get<Project>();
    EASEL_LOG("载入 %s：%s，%d 个点", path.c_str(), out->title.c_str(), (int)out->nodes.size());
    return true;
}

bool saveProject(const std::string& path, const Project& p) { return fs::saveJson(path, json(p)); }

// 一个能跑的小例子，省得第一次打开是空白（D-21）
Project makeExample(int n = 12) {
    Project p;
    p.title = "示例工程";
    p.unitScale = 10.0;
    Rng r(EASEL_FIXED_SEED);
    for (int i = 0; i < n; ++i)
        p.nodes.push_back(Node{"点" + std::to_string(i + 1), {r.d(-40, 40), r.d(-25, 25)}});
    return p;
}

// ============================================================================
//  4. 命令行入口（只有裸编译 / solver 这个 target 才会编进来）
//     app.cpp 和测试 #include 本文件时，EASEL_STANDALONE 没定义，这段就消失了。
// ============================================================================
#ifdef EASEL_STANDALONE
int main(int argc, char** argv) {
    cli::parse(argc, argv);          // 处理 --seed / --case / --iters …
    if (!cli::args().has("seed")) seed(EASEL_FIXED_SEED);   // 算法开发期固定种子：改一行代码前后好对比结果。做视觉作品可以删掉这一行
    installCrashHandler();           // 段错误也留下调用栈

    if (cli::args().has("doctor")) { std::cout << doctorCore(); return 0; }

    // --case debug/case-003.json：把 GUI 里导出的那一刻原样搬到命令行（D-23 第 7 条）
    std::string casePath = cli::args().str("case");
    if (!casePath.empty()) {
        json st = debug::importCase(casePath);
        if (st.contains("project")) S.project = st["project"].get<Project>();
        if (st.contains("params")) S.params = st["params"].get<Params>();
    } else {
        std::string path = cli::args().at(0, "data/example.json");
        if (!loadProject(path, &S.project)) {
            EASEL_LOG("用内置示例代替");
            S.project = makeExample();
        }
    }

    if (cli::args().has("iters")) S.params.iterations = (int)cli::args().num("iters", 200);

    Stopwatch sw;
    S.history = solve(S.project, S.params);
    std::cout << "用时 " << sw.ms() << " ms\n";
    if (!S.history.empty()) std::cout << "最终目标值 " << S.history.back().cost << "\n";

    // --dump-frame 5：把某一帧打出来，方便和 GUI 里看到的对比
    if (cli::args().has("dump-frame")) {
        int i = (int)cli::args().num("dump-frame", 0);
        if (i >= 0 && i < (int)S.history.size()) std::cout << json(S.history[i]).dump(2) << "\n";
    }
    return 0;
}
#endif
