// ============================================================================
//  test_solver.cpp —— 对拍与回归
//  把 solver.cpp 整个 include 进来，所以测的就是真正在跑的那份代码。
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "../src/solver.cpp"

TEST_CASE("cost：空的和只有一个点的都算 0") {
    Project p;
    CHECK(cost(p, {}) == doctest::Approx(0.0));
    p.nodes.push_back(Node{"甲", {0, 0}});
    CHECK(cost(p, {0}) == doctest::Approx(0.0));
}

TEST_CASE("cost：三个点连成的折线长度") {
    Project p;
    p.unitScale = 1.0;
    p.nodes = {Node{"甲", {0, 0}}, Node{"乙", {3, 4}}, Node{"丙", {3, 0}}};
    CHECK(cost(p, {0, 1, 2}) == doctest::Approx(5.0 + 4.0));
    CHECK(cost(p, {0, 2, 1}) == doctest::Approx(3.0 + 4.0));
}

TEST_CASE("solve：结果一定是一个合法的排列，而且不比初始解差") {
    Project p = makeExample(10);
    Params  params;
    params.iterations = 300;
    std::vector<Frame> h = solve(p, params);
    REQUIRE(!h.empty());

    std::vector<int> sorted = h.back().order;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < (int)sorted.size(); ++i) CHECK(sorted[i] == i);   // 是排列
    CHECK(h.back().cost <= h.front().cost + 1e-9);                        // 没变差
}

TEST_CASE("固定种子：同样的输入永远给同样的结果（D-19）") {
    Project p = makeExample(12);
    Params  params;
    seed(20260101);
    double a = solve(p, params).back().cost;
    seed(20260101);
    double b = solve(p, params).back().cost;
    CHECK(a == doctest::Approx(b));
}

// ---------------------------------------------------------------------------
//  对拍：拿「快的写法」和「笨但一定对的写法」比。
//  这里演示的是 cost 的两种算法；换成你自己的增量 / 重算就行。
// ---------------------------------------------------------------------------
TEST_CASE("对拍：增量算的和重算的必须一样") {
    auto gen = [](Rng& r) {
        Project p;
        int     n = r.i(2, 8);
        for (int i = 0; i < n; ++i) p.nodes.push_back(Node{"n", {r.d(-10, 10), r.d(-10, 10)}});
        return p;
    };
    auto fast = [](const Project& p) {
        std::vector<int> o(p.nodes.size());
        for (size_t i = 0; i < o.size(); ++i) o[i] = (int)i;
        return (long long)(cost(p, o) * 1000);
    };
    auto slow = [](const Project& p) {
        double t = 0;
        for (size_t i = 0; i + 1 < p.nodes.size(); ++i) {
            double dx = p.nodes[i + 1].pos.x - p.nodes[i].pos.x;
            double dy = p.nodes[i + 1].pos.y - p.nodes[i].pos.y;
            t += std::sqrt(dx * dx + dy * dy);
        }
        return (long long)(t * p.unitScale * 1000);
    };
    duipai::Report rep = EASEL_CROSSCHECK(200, gen, fast, slow);
    CHECK(rep.ok);
}

// 自己写 main，是为了在跑测试前把 EASEL_TRACE 的刷屏关掉（警告和错误照打）。
int main(int argc, char** argv) {
    easel::quiet(true);
    return doctest::Context(argc, argv).run();
}
