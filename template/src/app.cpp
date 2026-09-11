// ============================================================================
//  app.cpp —— 图形界面。main 就是一份接线清单，没有算法。
//
//  它把 solver.cpp 整个 include 进来（unity include，D-23）。
//  所以：改 solver.cpp -> 重编 app -> 立刻能在界面里看到，不用管头文件。
// ============================================================================
#include "solver.cpp"       // ← 你的算法、数据、全局 S 都在里面

#include <easel/easel.h>

// 界面自己的状态（和算法无关的都放这儿）
struct UiState {
    Timeline<Frame> tl;
    int             selected = -1;
    bool            showLabels = true;
    bool            compare = false;      // 对比模式：原方案 vs 优化后
    std::string     filePath;
    Texture         map;                  // 底图（可选）
};
UiState U;

static void runSolve(App& app) {
    S.history = solve(S.project, S.params);
    U.tl.load(S.history);
    U.tl.play();

    // 把所有点装进画布
    std::vector<Vec2> pts;
    for (const Node& n : S.project.nodes) pts.push_back(n.pos);
    app.camera().fit(Rect::bounding(pts).expanded(5));

    app.status(S.project.title + "：共 " + std::to_string((int)S.history.size()) + " 帧");
}

int main(int argc, char** argv) {
    App app(argc, argv);
    if (!cli::args().has("seed")) seed(EASEL_FIXED_SEED);   // 算法开发期固定种子：改一行代码前后好对比结果。做视觉作品可以删掉这一行
    app.title("我的作品").size(1280, 800).theme(Theme::Forest());
    app.statusBar(true);   // 底部状态栏：状态灯 + app.status() 的文字。交付时可以关掉

    // ---- 启动时的数据：--open 优先，其次内置示例（D-21）----
    std::string path = app.openPath();
    if (path.empty() || !loadProject(path, &S.project)) S.project = makeExample();
    U.filePath = path;

    // 底图要等显卡准备好才能加载，所以放在 onStart 里
    app.onStart([&] {
        // 想铺一张底图（地图、平面图、照片）就把它放进 assets/，改成你的文件名
        if (fs::exists("assets/map.png")) U.map = loadTexture("assets/map.png");
    });

    // ---- 画布 ----
    app.onDraw([&](Canvas& c) {
        // 底图铺在世界坐标的这一块上。换成你自己的图时改这个矩形。
        if (U.map) c.image(U.map, Rect(-60, -40, 120, 80));

        const Frame& f = U.tl.current();     // 时间线是空的时候会给一个默认帧，不用判空
        const std::vector<Node>& nodes = S.project.nodes;

        // 路线
        std::vector<Vec2> pts;
        for (int i : f.order)
            if (i >= 0 && i < (int)nodes.size()) pts.push_back(nodes[i].pos);
        if (U.compare) {                                   // 现状：橙色虚线
            std::vector<Vec2> orig;
            for (const Node& n : nodes) orig.push_back(n.pos);
            c.stroke(app.theme().accent2, 2.0).dashed(6, 5).polyline(orig);
            c.solid();
        }
        c.stroke(app.theme().accent, 3.0).polyline(pts);   // 优化后：主色实线

        // 点
        for (int i = 0; i < (int)nodes.size(); ++i) {
            bool hot = (i == U.selected) || (i == f.focus);
            c.fill(hot ? app.theme().accent2 : app.theme().good).stroke(Color::gray(0.1f), 1.5);
            c.dot(nodes[i].pos, hot ? 9 : 6);
            if (U.showLabels) {
                // 底图是浅色的，所以名字用深色写；换成深色底图记得把这行也改了
                c.noStroke().fill(Color::hex(0x1B2119)).textSize(13);
                c.text(nodes[i].pos + Vec2(0, -18 / c.zoom()), nodes[i].name, Align::Center);
            }
        }
    });

    // ---- 右侧面板 ----
    app.onPanel([&] {
        if (ui::section("工程")) {
            ui::stat("点数", (int)S.project.nodes.size(), "个");
            if (!U.tl.empty()) ui::stat("当前目标值", U.tl.current().cost, "", 1);
            if (ui::button("打开…")) {
                std::string p = file::open("工程文件", "json");
                if (!p.empty() && loadProject(p, &S.project)) { U.filePath = p; runSolve(app); }
            }
            ui::sameLine();
            if (ui::button("保存…")) {
                std::string p = file::save("project.json");
                if (!p.empty()) saveProject(p, S.project);
            }
        }
        if (ui::section("参数")) {
            ui::slider("迭代次数", &S.params.iterations, 10, 5000);
            ui::slider("系数 alpha", &S.params.alpha, 0.0, 1.0);
            if (ui::button("开始优化", true)) runSolve(app);
        }
        if (ui::section("显示")) {
            ui::toggle("显示名称", &U.showLabels);
            ui::toggle("对比：现状 vs 优化后", &U.compare);
            // 主题就是数据，五套预设一行切换（D-22）
            static int themeIndex = 0;
            if (ui::select("配色", &themeIndex, {"Forest 绿", "Ocean 蓝", "Ember 橙", "Paper 米白", "Slate 灰蓝"}))
                app.theme(Theme::presets()[themeIndex]);
        }
        if (ui::section("收敛曲线")) {
            std::vector<double> ys;
            for (const Frame& f : U.tl.frames()) ys.push_back(f.cost);
            ui::chart("##conv", ys, "目标值");
        }
    });

    // ---- 交互：点一下选中离鼠标最近的点 ----
    app.onClick([&](Vec2 w, Mouse b) {
        if (b != Mouse::Left) return;
        int    best = -1;
        double bd = 1e18;
        for (int i = 0; i < (int)S.project.nodes.size(); ++i) {
            double d = dist(w, S.project.nodes[i].pos);
            if (d < bd) { bd = d; best = i; }
        }
        U.selected = (bd * app.camera().zoom() < 20) ? best : -1;
        if (U.selected >= 0) EASEL_LOG("选中了 %s", S.project.nodes[U.selected].name.c_str());
    });

    // ---- 拖拽：把选中的点挪个位置 ----
    app.onDrag([&](const Drag& d) {
        if (d.button == Mouse::Left && U.selected >= 0)
            S.project.nodes[U.selected].pos += d.delta;
    });

    // ---- 调试台的「导出调试用例」要导出什么（D-23 第 7 条）----
    app.onExportCase([] {
        return json{{"project", S.project}, {"params", S.params}};
    });

    // ---- 启动页：先让人看到东西，别给一块白板（D-21）----
    app.welcome("我的作品", "选一个你想解决的问题，用算法算它，用画面讲清楚它。",
                [&] {
                    if (ui::button("打开示例数据（推荐）", true)) { runSolve(app); app.closeWelcome(); }
                    if (ui::button("打开我的文件…", true)) {
                        std::string p = file::open("工程文件", "json");
                        if (!p.empty() && loadProject(p, &S.project)) { runSolve(app); app.closeWelcome(); }
                    }
                    if (ui::button("从空白开始", true)) app.closeWelcome();
                });

    app.transport(U.tl);

    // ---- 命令行快启：app --open data/example.json --solve ----
    if (app.wantsSolve()) { runSolve(app); app.closeWelcome(); }

    return app.run();
}
