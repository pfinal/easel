// ============================================================================
//  app.cpp —— 界面写在这里
//
//  main 就是一份接线清单：窗口多大、画什么、面板上有哪些控件。
//  算法一行都不在这儿 —— 它在 solver.cpp 里，下面这句 #include 把它整个搬进来。
// ============================================================================
#include <easel/easel.h>

#include "solver.cpp"

using namespace easel;

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("我的作品").size(1280, 800).theme(Theme::Forest());

    // 窗口和显卡都准备好之后调一次
    app.onStart([&app] {
        solve();
        // 把算出来的东西一键装进画布（之后滚轮缩放、中键平移都是白送的）
        app.camera().fit(Rect::bounding(S.points), 80);
    });

    // 每帧重画一次整张画布。坐标是「世界坐标」，滚轮缩放、中键平移都是白送的。
    app.onDraw([&](Canvas& c) {
        const Theme& th = app.theme();

        c.stroke(th.accent, 2);
        c.polyline(S.points, true);          // 把点连成一圈

        c.fill(th.accent);
        for (const Vec2& p : S.points) c.dot(p, 6);

        c.fill(th.fg);
        c.text({0, 0}, "Hello, Easel", Align::Center);   // 世界坐标，跟着缩放一起动
    });

    // 右边的面板。变量就是唯一真相源：拖滑块直接改 S 里的值。
    app.onPanel([] {
        if (ui::section("参数")) {
            if (ui::slider("点数", &S.count, 3, 40)) solve();
            if (ui::slider("半径", &S.radius, 5.0, 60.0)) solve();
        }
        ui::help("拖滑块就重算一次 —— 变量是唯一真相源，界面只是它的影子。");
        if (ui::button("重新算一次", true)) solve();
        ui::stat("点数", (int)S.points.size(), "个");
    });

    // 想看更完整的写法（读数据文件、逐帧回放、收敛曲线、导出调试用例），
    // 看 Easel 仓库里的 examples/sort，或者新建工程时勾上「带回放的完整骨架」。
    return app.run();
}
