// examples/hello —— 一圈点 + 两个滑块。Easel 最简单的可交互例子：State 是唯一真相源，
// 拖一下滑块就重算一次，onDraw 每帧照着 State 重画。
#include <easel/easel.h>

#include <cmath>

using namespace easel;

// ---- 数据：一个全局变量就够，不用写 class ---------------------------------
struct State {
    int    count = 8;      // 画几个点
    double radius = 20.0;  // 撒多开

    std::vector<Vec2> points;   // 算出来的结果
};
State state;

// ---- 算法：换成你自己的 ----------------------------------------------------
// 现在这份只是让例子一眼看懂：把 count 个点均匀摆成一圈。
static void solve() {
    state.points.clear();
    for (int i = 0; i < state.count; ++i) {
        double a = 2 * 3.14159265358979 * i / state.count;
        state.points.push_back({std::cos(a) * state.radius, std::sin(a) * state.radius});
    }
    EASEL_LOG("算完了：%d 个点", (int)state.points.size());
}

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("Hello, Easel").size(1280, 800).theme(Theme::Forest());

    // 窗口和显卡都准备好之后调一次
    app.onStart([&app] {
        solve();
        // 把算出来的东西一键装进画布（之后滚轮缩放、中键平移都是白送的）
        app.camera().fit(Rect::bounding(state.points), 80);
    });

    // 每帧重画一次整张画布。坐标是「世界坐标」，滚轮缩放、中键平移都是白送的。
    app.onDraw([&](Canvas& c) {
        const Theme& th = app.theme();

        c.stroke(th.accent, 2);
        c.polyline(state.points, true);          // 把点连成一圈

        c.fill(th.accent);
        for (const Vec2& p : state.points) c.dot(p, 6);

        c.fill(th.fg);
        c.text({0, 0}, "Hello, Easel", Align::Center);   // 世界坐标，跟着缩放一起动
    });

    // 右边的面板。变量就是唯一真相源：拖滑块直接改 state 里的值。
    app.onPanel([] {
        if (ui::section("参数")) {
            if (ui::slider("点数", &state.count, 3, 40)) solve();
            if (ui::slider("半径", &state.radius, 5.0, 60.0)) solve();
        }
        ui::help("拖滑块就重算一次 —— 变量是唯一真相源，界面只是它的影子。");
        if (ui::button("重新算一次", true)) solve();
        ui::stat("点数", (int)state.points.size(), "个");
    });

    return app.run();
}
