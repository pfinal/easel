// Graphics（离屏画布）的测试：不开窗口、没有渲染后端时的行为。
// 真正画得对不对要靠 examples/creative --part 4 配 --screenshot 拿眼睛看；
// 这份测试只管一件事——没有显卡上下文时 create() 必须老老实实返回 false，不崩、
// 也不留下半初始化的状态（valid() 还是假的，texture() 还是空的）。
#include <doctest/doctest.h>

#include <easel/canvas.h>

using namespace easel;

TEST_CASE("Graphics：默认构造是无效的") {
    Graphics g;
    CHECK(g.valid() == false);
    CHECK(g.width() == 0);
    CHECK(g.height() == 0);
}

TEST_CASE("Graphics：没有渲染后端时 create() 优雅失败，不崩") {
    Graphics g;
    CHECK(g.create(64, 64) == false);   // 测试进程没跑 App::run()，没有 GL 上下文
    CHECK(g.valid() == false);
    CHECK(g.width() == 0);
    CHECK(g.height() == 0);
    Texture t = g.texture();
    CHECK(!t);   // 空 Texture：id == 0
}

TEST_CASE("Graphics：尺寸非法直接拒绝") {
    Graphics g;
    CHECK(g.create(0, 100) == false);
    CHECK(g.create(100, 0) == false);
    CHECK(g.create(-5, 10) == false);
}

TEST_CASE("Graphics：begin()/end()/clear() 在无效状态下不崩") {
    Graphics g;
    Canvas&  c = g.begin();   // 没建成功，画什么都该被静默吃掉
    c.stroke(Color::hex(0xFF0000), 2).line({0, 0}, {10, 10});
    g.end();
    g.clear();
    g.destroy();   // 没建过也能 destroy，不崩
}

TEST_CASE("Graphics：move 语义——移动之后旧对象归零，不崩") {
    Graphics a;
    Graphics b(std::move(a));
    CHECK(a.valid() == false);
    CHECK(b.valid() == false);

    Graphics c, d;
    d = std::move(c);
    CHECK(c.valid() == false);
    CHECK(d.valid() == false);
}
