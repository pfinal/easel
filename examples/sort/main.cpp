// examples/sort —— 排序可视化。Easel 的全部词汇都在这 80 行里。
#include <easel/easel.h>
using namespace easel;

struct Frame { std::vector<int> v; int a = -1, b = -1; long long cmp = 0, swp = 0; };
struct State { int n = 40; int algo = 0; std::vector<int> data; Timeline<Frame> tl; };
State S;

static void makeData() {
    S.data.resize(S.n);
    for (int i = 0; i < S.n; ++i) S.data[i] = i + 1;
    rng().shuffle(S.data);                       // 库默认随机种子：每次打开都是新的一批数据（D-34）
    S.tl.clear();
}

static void snap(const std::vector<int>& v, int a, int b, long long c, long long s) {
    if (S.tl.size() > 3000) return;              // 帧数封顶，别把内存吃光
    S.tl.push(Frame{v, a, b, c, s});
    EASEL_TRACE("比较次数", (double)c);          // 命令行打 CSV，App 里自动画折线
}

static void sortAll() {
    std::vector<int> v = S.data;
    long long c = 0, s = 0;
    S.tl.clear(); snap(v, -1, -1, 0, 0);
    int n = (int)v.size();
    for (int i = 0; i < n - 1; ++i) {
        if (S.algo == 0) {                       // 冒泡：一趟一趟把最大的顶到右边
            for (int j = 0; j < n - 1 - i; ++j) {
                ++c;
                if (v[j] > v[j + 1]) { std::swap(v[j], v[j + 1]); ++s; snap(v, j, j + 1, c, s); }
            }
        } else {                                 // 选择：一趟挑一个最小的换到左边
            int best = i;
            for (int j = i + 1; j < n; ++j) { ++c; if (v[j] < v[best]) best = j; }
            if (best != i) { std::swap(v[i], v[best]); ++s; }
            snap(v, i, best, c, s);
        }
    }
    snap(v, -1, -1, c, s);
    EASEL_LOG("排完了：比较 %lld 次，交换 %lld 次，共 %d 帧", c, s, (int)S.tl.size());
    EASEL_CHECK(std::is_sorted(v.begin(), v.end()), "排完之后居然不是有序的");
    S.tl.play();
}

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("排序可视化").size(1180, 720).theme(Theme::Ocean());
    app.statusBar(true);
    makeData(); sortAll();
    Color hot = app.theme().accent2, cool = app.theme().accent;

    app.onDraw([&](Canvas& c) {
        const Frame& f = S.tl.current();
        c.camera().fit(Rect(0, -2, (double)S.n, (double)S.n + 2));
        c.noStroke();
        for (int i = 0; i < (int)f.v.size(); ++i) {
            c.fill((i == f.a || i == f.b) ? hot : cool.mix(Color::gray(0.9f), f.v[i] / (float)S.n));
            c.rect(Rect(i + 0.12, S.n - f.v[i], 0.76, (double)f.v[i]));
        }
        c.fill(Color::gray(0.85f)).textSize(14);
        c.text(Vec2(0, -0.6), "第 " + std::to_string(S.tl.index() + 1) + " / " +
                                  std::to_string((int)S.tl.size()) + " 帧");
    });

    app.onPanel([&] {
        if (ui::section("数据")) {
            if (ui::slider("元素个数", &S.n, 8, 120)) { makeData(); sortAll(); }
            const char* algos[] = {"冒泡排序", "选择排序"};
            if (ImGui::Combo("算法", &S.algo, algos, 2)) sortAll();   // Easel 没有的直接调 ImGui
            if (ui::button("换一批数据", true)) { seed(current_seed() + 1); makeData(); sortAll(); }
        }
        const Frame& f = S.tl.current();
        if (ui::section("这一帧")) {
            ui::stat("比较次数", (int)f.cmp); ui::stat("交换次数", (int)f.swp);
            ui::stat("总帧数", (int)S.tl.size());
        }
        if (ui::section("收敛曲线")) {
            std::vector<double> ys;
            for (const Frame& x : S.tl.frames()) ys.push_back((double)x.cmp);
            ui::chart("##cmp", ys, "比较次数");
        }
        ui::help("空格 + 左键拖拽平移，滚轮缩放，F12 打开调试台。");
        app.status("Easel 排序示例 · 一共 80 行");
    });

    app.transport(S.tl);
    app.onExportCase([] { return json{{"n", S.n}, {"algo", S.algo}, {"data", S.data}}; });
    return app.run();
}
