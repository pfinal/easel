// 画布创意包的测试（D-33 批次 1）。
// 这里不开窗口、不碰渲染后端：只测两样能纯算出来的东西 ——
//   1. Canvas 的变换栈（transform() 的矩阵数学）
//   2. Layer 的记录 / 清空 / 上限
// 画出来长什么样要靠 examples 和眼睛，测试管不了；但矩阵算错了这里一定会红。
// Layer::Cmd/Kind/commands() 是 Easel 内部实现（B6，只 friend 给 Canvas），
// 这份测试只走 Layer 的公开接口（size()/clear()/limit()），不再深入到每条命令的细节。
#include <doctest/doctest.h>

#include <easel/canvas.h>

using namespace easel;

static const double kPi = 3.14159265358979323846;

TEST_CASE("变换栈：默认是单位阵") {
    Canvas c;
    CHECK(c.transform({3, 4}).x == doctest::Approx(3));
    CHECK(c.transform({3, 4}).y == doctest::Approx(4));
}

TEST_CASE("变换栈：平移") {
    Canvas c;
    c.translate({10, -5});
    CHECK(c.transform({0, 0}).x == doctest::Approx(10));
    CHECK(c.transform({0, 0}).y == doctest::Approx(-5));
    CHECK(c.transform({1, 1}).x == doctest::Approx(11));
    // 平移可以累加
    c.translate({1, 1});
    CHECK(c.transform({0, 0}).x == doctest::Approx(11));
    CHECK(c.transform({0, 0}).y == doctest::Approx(-4));
}

TEST_CASE("变换栈：旋转和 Vec2::rotated 同向") {
    Canvas c;
    c.rotate(kPi / 2);
    Vec2 p = c.transform({1, 0});
    CHECK(p.x == doctest::Approx(0).epsilon(1e-9));
    CHECK(p.y == doctest::Approx(1));
    Vec2 q = Vec2{2, 3}.rotated(0.7);
    Canvas d;
    d.rotate(0.7);
    CHECK(d.transform({2, 3}).x == doctest::Approx(q.x));
    CHECK(d.transform({2, 3}).y == doctest::Approx(q.y));
}

TEST_CASE("变换栈：缩放（等比和分轴）") {
    Canvas c;
    c.scale(2);
    CHECK(c.transform({3, 4}).x == doctest::Approx(6));
    CHECK(c.transform({3, 4}).y == doctest::Approx(8));
    c.resetMatrix();
    c.scale(2, -1);                       // sy 为负 = 上下翻转
    CHECK(c.transform({3, 4}).x == doctest::Approx(6));
    CHECK(c.transform({3, 4}).y == doctest::Approx(-4));
}

TEST_CASE("变换栈：后写的先作用在点上（Processing 的顺序）") {
    // translate(10,5) 再 rotate(90°)：点先转再挪
    Canvas c;
    c.translate({10, 5}).rotate(kPi / 2);
    Vec2 p = c.transform({1, 0});             // (1,0) --转--> (0,1) --挪--> (10,6)
    CHECK(p.x == doctest::Approx(10));
    CHECK(p.y == doctest::Approx(6));

    // 反过来写结果就不一样 —— 这是学生最容易踩的一脚
    Canvas d;
    d.rotate(kPi / 2).translate({10, 5});
    Vec2 q = d.transform({1, 0});             // (1,0) --挪--> (11,5) --转--> (-5,11)
    CHECK(q.x == doctest::Approx(-5));
    CHECK(q.y == doctest::Approx(11));
}

TEST_CASE("变换栈：translate + rotate + scale 组合") {
    Canvas c;
    c.translate({100, 100}).rotate(kPi).scale(3);
    Vec2 p = c.transform({1, 0});             // 放大 3 倍 -> (3,0)，转半圈 -> (-3,0)，挪过去
    CHECK(p.x == doctest::Approx(97));
    CHECK(p.y == doctest::Approx(100).epsilon(1e-9));
}

TEST_CASE("push/pop：矩阵和样式一起存、一起还原") {
    Canvas c;
    c.translate({5, 5});
    c.push();
    c.translate({100, 0}).rotate(kPi / 4).scale(9);
    CHECK(c.transform({0, 0}).x == doctest::Approx(105));
    c.pop();
    CHECK(c.transform({0, 0}).x == doctest::Approx(5));   // 回到 push 时的样子
    CHECK(c.transform({0, 0}).y == doctest::Approx(5));

    // 样式也一起还原
    c.fill(Color::hex(0x112233));
    c.push();
    c.fill(Color::hex(0xAABBCC)).noStroke();
    CHECK(c.style().hasStroke == false);
    c.pop();
    CHECK(c.style().hasStroke == true);
    CHECK(c.style().fillColor.r == doctest::Approx(0x11 / 255.0).epsilon(1e-4));

    // 栈空了再 pop 不该崩
    c.pop();
    c.pop();
    CHECK(c.transform({1, 1}).x == doctest::Approx(6));
}

TEST_CASE("push/pop 可以套很多层") {
    Canvas c;
    for (int i = 0; i < 20; ++i) {
        c.push();
        c.translate({1, 0});
    }
    CHECK(c.transform({0, 0}).x == doctest::Approx(20));
    for (int i = 0; i < 20; ++i) c.pop();
    CHECK(c.transform({0, 0}).x == doctest::Approx(0));
}

TEST_CASE("resetMatrix 清成单位阵，但不动样式") {
    Canvas c;
    c.translate({7, 7}).rotate(1.0).noFill();
    c.resetMatrix();
    CHECK(c.transform({2, 3}).x == doctest::Approx(2));
    CHECK(c.transform({2, 3}).y == doctest::Approx(3));
    CHECK(c.style().hasFill == false);
}

TEST_CASE("Layer：记下来的条数") {
    Layer ink;
    CHECK(ink.size() == 0);
    ink.line({0, 0}, {1, 1});
    ink.dot({2, 2});
    ink.circle({0, 0}, 3);
    ink.rect(Rect(0, 0, 1, 1));
    ink.triangle({0, 0}, {1, 0}, {0, 1});
    ink.polygon({{0, 0}, {1, 0}, {1, 1}});
    ink.polyline({{0, 0}, {1, 1}, {2, 0}});
    ink.ellipse({0, 0}, 2, 1);
    ink.text({0, 0}, "你好");
    CHECK(ink.size() == 9);
    ink.clear();
    CHECK(ink.size() == 0);
}

TEST_CASE("Layer：stroke()/noFill() 改的是「当前」样式，落笔按顺序累加条数") {
    // Layer::Cmd/Kind/commands() 是内部实现（B6，只 friend 给 Canvas），学生和测试
    // 都摸不到「历史上第几条命令用了什么样式」——只能看 style() 这个「此刻」的状态。
    Layer ink;
    ink.stroke(Color::hex(0xFF0000), 4);
    CHECK(ink.style().strokeColor.r == doctest::Approx(1.0));
    CHECK(ink.style().strokeWidth == doctest::Approx(4));
    CHECK(ink.style().hasFill == true);
    ink.line({0, 0}, {1, 0});
    CHECK(ink.size() == 1);

    ink.stroke(Color::hex(0x00FF00), 1).noFill();
    CHECK(ink.style().strokeColor.g == doctest::Approx(1.0));
    CHECK(ink.style().strokeWidth == doctest::Approx(1));
    CHECK(ink.style().hasFill == false);
    ink.line({1, 0}, {2, 0});
    CHECK(ink.size() == 2);
}

TEST_CASE("Layer：上限，超了丢最早的（数量层面；具体丢了哪条是内部实现，见不到）") {
    Layer ink;
    CHECK(ink.limit() == 200000);          // 默认 20 万条
    ink.limit(4);
    for (int i = 0; i < 10; ++i) ink.dot({(double)i, 0});
    CHECK(ink.size() == 4);                // 超出上限的部分被丢掉，留下的正好是上限条数

    // 调小上限，多出来的立刻丢掉
    ink.limit(2);
    CHECK(ink.size() == 2);

    // limit(0) 没意义，至少留一条，别让 push 无处可放
    ink.limit(0);
    CHECK(ink.limit() == 1);
    ink.dot({42, 0});
    CHECK(ink.size() == 1);
}

TEST_CASE("Layer：折线/多边形各类图元都能落笔，条数照记") {
    Layer              ink;
    std::vector<Vec2>  pts = {{0, 0}, {1, 2}, {3, 1}};
    ink.polyline(pts, true);
    CHECK(ink.size() == 1);
    ink.polygon(pts);
    CHECK(ink.size() == 2);
}
