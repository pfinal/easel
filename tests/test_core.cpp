// Easel 自己的测试（core 部分 —— 没有 GUI，CI 上无头也能跑）
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <easel/core.h>

using namespace easel;

TEST_CASE("Vec2 基本运算") {
    Vec2 a{3, 4};
    CHECK(a.length() == doctest::Approx(5.0));
    CHECK((a * 2).x == doctest::Approx(6.0));
    CHECK(dist({0, 0}, {3, 4}) == doctest::Approx(5.0));
    CHECK(dot({1, 0}, {0, 1}) == doctest::Approx(0.0));
    CHECK(cross({1, 0}, {0, 1}) == doctest::Approx(1.0));
    CHECK(a.normalized().length() == doctest::Approx(1.0));
    CHECK(lerp(Vec2{0, 0}, Vec2{10, 0}, 0.25).x == doctest::Approx(2.5));
    Vec2 r = Vec2{1, 0}.rotated(3.14159265358979 / 2);
    CHECK(r.y == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("Rect 包围盒与命中") {
    Rect r = Rect::bounding({{0, 0}, {10, 5}, {-2, 7}});
    CHECK(r.left() == doctest::Approx(-2));
    CHECK(r.right() == doctest::Approx(10));
    CHECK(r.bottom() == doctest::Approx(7));
    CHECK(r.contains({0, 0}));
    CHECK_FALSE(r.contains({100, 100}));
    CHECK(r.overlaps(Rect(5, 5, 20, 20)));
    CHECK(Rect(0, 0, 2, 2).expanded(1).w == doctest::Approx(4));
    CHECK(Rect::fromCenter({0, 0}, 4, 4).left() == doctest::Approx(-2));
}

TEST_CASE("Color 转换") {
    Color c = Color::hex(0x2E7D32);
    CHECK(c.r == doctest::Approx(0x2E / 255.0).epsilon(1e-4));
    CHECK(Color::rgb(255, 0, 0).rgba32() == 0xFF0000FFu);   // AABBGGRR
    CHECK(Color::hsv(0, 1, 1).r == doctest::Approx(1.0));
    CHECK(Color::gray(0.5f).withAlpha(0.25f).a == doctest::Approx(0.25));
}

TEST_CASE("json：Vec2 / Rect / Color 与自定义结构体") {
    Vec2 v{1.5, -2.5};
    json j = v;
    CHECK(j.dump() == "[1.5,-2.5]");
    CHECK(j.get<Vec2>().y == doctest::Approx(-2.5));

    struct Node { std::string name; Vec2 pos; int n = 0; };
    // EASEL_JSON 只能写在命名空间作用域，这里手写等价代码验证往返
    json k = json{{"name", "甲"}, {"pos", json::array({1, 2})}, {"n", 3}};
    Node node{k["name"], k["pos"].get<Vec2>(), k["n"]};
    CHECK(node.name == "甲");
    CHECK(node.pos.x == doctest::Approx(1.0));
}

TEST_CASE("固定种子：同一个种子给同一串数（D-19）") {
    Rng a(123), b(123);
    for (int i = 0; i < 20; ++i) CHECK(a.i(0, 1000) == b.i(0, 1000));
    seed(20260101);
    int first = rng().i(0, 1000000);
    seed(20260101);
    CHECK(rng().i(0, 1000000) == first);
}

TEST_CASE("seed() 连带换 noise()：同一个种子给同一片山（D-34）") {
    seed(555);
    double a = noise(1.5);
    seed(9999);                 // 先换到别的种子
    seed(555);                  // 再换回来
    double b = noise(1.5);
    CHECK(a == doctest::Approx(b));
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("两个不同的种子，rng().d() 给出不一样的序列（D-34）") {
    seed(1);
    std::vector<double> a;
    for (int i = 0; i < 20; ++i) a.push_back(rng().d());
    seed(2);
    std::vector<double> b;
    for (int i = 0; i < 20; ++i) b.push_back(rng().d());
    int diff = 0;
    for (int i = 0; i < 20; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-12) ++diff;
    CHECK(diff > 0);
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("remap：把一段范围搬到另一段（对应 Processing 的 map）") {
    CHECK(remap(0, 0, 1, 10, 20) == doctest::Approx(10));      // 左端点
    CHECK(remap(1, 0, 1, 10, 20) == doctest::Approx(20));      // 右端点
    CHECK(remap(0.5, 0, 1, 10, 20) == doctest::Approx(15));
    CHECK(remap(2, 0, 1, 0, 10) == doctest::Approx(20));       // 超出范围不夹紧
    CHECK(remap(2.5, 0, 10, 10, 0) == doctest::Approx(7.5));   // 可以反着映射
    CHECK(remap(3, 1, 1, 7, 9) == doctest::Approx(7));         // lo == hi 不除零
}

TEST_CASE("noise：范围 [0,1]、确定性、换种子就换一串") {
    noiseSeed(EASEL_FIXED_SEED);
    noiseDetail(4, 0.5);
    for (int i = 0; i < 200; ++i) {
        double a = noise(i * 0.05);
        double b = noise(i * 0.05, i * 0.03);
        double c = noise(i * 0.05, i * 0.03, i * 0.02);
        CHECK(a >= 0.0);
        CHECK(a <= 1.0);
        CHECK(b >= 0.0);
        CHECK(b <= 1.0);
        CHECK(c >= 0.0);
        CHECK(c <= 1.0);
    }

    // 同一个种子 -> 同一串数（D-19）
    noiseSeed(7);
    std::vector<double> first;
    for (int i = 0; i < 50; ++i) first.push_back(noise(i * 0.1, i * 0.07));
    noiseSeed(7);
    for (int i = 0; i < 50; ++i) CHECK(noise(i * 0.1, i * 0.07) == doctest::Approx(first[i]));

    // 换个种子就该是另一片山
    noiseSeed(8);
    int diff = 0;
    for (int i = 0; i < 50; ++i)
        if (std::fabs(noise(i * 0.1, i * 0.07) - first[i]) > 1e-9) ++diff;
    CHECK(diff > 0);

    // 和 rng() 的区别：相邻的输入给相邻的输出（画出来才是山不是雪花点）
    noiseSeed(EASEL_FIXED_SEED);
    CHECK(std::fabs(noise(3.0) - noise(3.001)) < 0.01);
    // 层数变了，同一个坐标的值也该变
    double four = noise(1.7, 2.3);
    noiseDetail(1);
    CHECK(std::fabs(noise(1.7, 2.3) - four) > 1e-9);
    noiseDetail(4, 0.5);
}

TEST_CASE("Rng::shuffle 是一个排列") {
    std::vector<int> v(50);
    for (int i = 0; i < 50; ++i) v[i] = i;
    Rng r(7);
    r.shuffle(v);
    std::vector<int> s = v;
    std::sort(s.begin(), s.end());
    for (int i = 0; i < 50; ++i) CHECK(s[i] == i);
}

TEST_CASE("命令行参数") {
    const char* argv[] = {"./solver", "data/a.json", "--seed", "42", "--iters=300", "--solve"};
    cli::parse(6, (char**)argv);
    CHECK(cli::args().has("solve"));
    CHECK_FALSE(cli::args().has("nope"));
    CHECK(cli::args().num("seed", 0) == 42);
    CHECK(cli::args().num("iters", 0) == 300);
    CHECK(cli::args().at(0) == "data/a.json");
    CHECK(current_seed() == 42u);        // --seed 会立刻生效
    cli::parse(0, nullptr);              // 清掉，别影响后面的用例
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("命令行参数：伪造的 argv 必须原样解析（不能被真实命令行替换掉）") {
    // 这条在所有平台都要过；在 Windows 上它验证的正是 cli::parse 里那条「传进来的 argv
    // 是不是就是这个进程的真实命令行」判定——argc 对不上（这里是 5，测试可执行文件自己
    // 跑起来的 argc 通常不是 5），判定不成立，就必须原样用下面这份伪造的 argv，而不是被
    // GetCommandLineW() 取到的 easel_tests.exe 自己的命令行覆盖掉。
    const char* argv[] = {"./fake-program", "--seed", "42", "--open", "x.json"};
    cli::parse(5, (char**)argv);
    CHECK(cli::args().str("open") == "x.json");
    CHECK(current_seed() == 42u);
    cli::parse(0, nullptr);              // 清掉，别影响后面的用例
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("文件读写与 JSON 往返") {
    std::string path = "build_test_tmp/x.json";
    json        j = json{{"a", 1}, {"b", json::array({1, 2, 3})}};
    REQUIRE(fs::saveJson(path, j));
    CHECK(fs::exists(path));
    CHECK(fs::loadJson(path) == j);
    CHECK(fs::loadJson("完全不存在的文件.json").is_null());   // 不抛异常，返回 null
    std::remove(path.c_str());
}

TEST_CASE("调试用例：导出之后能原样读回来，种子也恢复") {
    cli::parse(0, nullptr);              // 命令行上没有 --seed，用例里的种子说了算
    debug::dir() = "build_test_debug";
    seed(4242);
    json        state = json{{"nodes", json::array({1, 2, 3})}};
    std::string p = debug::exportCase(state, "单元测试");
    REQUIRE(!p.empty());
    seed(1);
    json back = debug::importCase(p);
    CHECK(back == state);
    CHECK(current_seed() == 4242u);
    std::remove(p.c_str());
    debug::dir() = "debug";
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("调试用例：命令行显式给了 --seed 就听命令行的（换种子重跑）") {
    debug::dir() = "build_test_debug";
    cli::parse(0, nullptr);
    seed(4242);
    std::string p = debug::exportCase(json{{"x", 1}}, "");
    REQUIRE(!p.empty());

    const char* argv[] = {"./solver", "--case", p.c_str(), "--seed", "777"};
    cli::parse(5, (char**)argv);
    CHECK(current_seed() == 777u);
    debug::importCase(p);
    CHECK(current_seed() == 777u);       // 用例里写的是 4242，但命令行赢

    std::remove(p.c_str());
    cli::parse(0, nullptr);
    debug::dir() = "debug";
    seed(EASEL_FIXED_SEED);
}

TEST_CASE("对拍器：两个一样的函数应该全过") {
    auto gen = [](Rng& r) { return r.i(1, 100); };
    auto fa = [](int x) { return x * 2; };
    auto fb = [](int x) { return x + x; };
    duipai::Report rep = duipai::run(50, gen, fa, fb);
    CHECK(rep.ok);
    CHECK(rep.rounds == 50);
}

TEST_CASE("对拍器：不一样时能指出是第几轮、用哪个种子") {
    debug::dir() = "build_test_debug";
    auto gen = [](Rng& r) { return r.i(1, 100); };
    auto bad = [](int x) { return x > 50 ? x : x * 2; };
    auto good = [](int x) { return x * 2; };
    duipai::Report rep = duipai::run(200, gen, bad, good);
    CHECK_FALSE(rep.ok);
    CHECK(rep.failedRound >= 0);
    CHECK(!rep.casePath.empty());
    if (!rep.casePath.empty()) std::remove(rep.casePath.c_str());
    debug::dir() = "debug";
}

TEST_CASE("bench 与 Stopwatch") {
    Stopwatch sw;
    bench::Result r = bench::run([] { volatile int x = 0; for (int i = 0; i < 1000; ++i) x += i; }, 3);
    CHECK(r.repeats == 3);
    CHECK(r.bestMs >= 0.0);
    CHECK(r.avgMs >= r.bestMs - 1e-9);
    CHECK(sw.ms() >= 0.0);
}

TEST_CASE("doctorCore 里该有的都有") {
    std::string d = doctorCore();
    CHECK(d.find("Easel") != std::string::npos);
    CHECK(d.find("随机种子") != std::string::npos);
    CHECK(d.find("工作目录") != std::string::npos);
}

// 自己写 main，是为了在跑测试前把 EASEL_TRACE 的刷屏关掉（警告和错误照打）。
int main(int argc, char** argv) {
    easel::quiet(true);
    return doctest::Context(argc, argv).run();
}
