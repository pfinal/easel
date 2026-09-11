// examples/creative —— 创意包合集：变换栈、Layer、noise、精灵动画与声音、离屏画布。
// 一个窗口五个小节，面板上一个下拉切换；命令行 --part 0..4 可以直接跳到某一节（截图用）。
#include <easel/easel.h>
using namespace easel;
// kPi 是 core.h 里的 easel::kPi（上面 `using namespace easel` 已经带进来了）。

struct State {
    int      part = 0;      // 0 万花筒 1 拖尾 2 噪声地形 3 精灵动画 + 声音 4 流场
    double   t = 0;
    Layer    ink;            // 拖尾用：Scratch 的画笔
    bool     hasLast = false;
    Vec2     last;
    unsigned seedVal = 7;    // 地形的噪声种子
    Texture  sprite;         // 4 帧 32x32 精灵表，只能在 onStart 里加载

    // ---- 流场（离屏画布 Graphics 的验收场景）----
    // 几千个粒子沿噪声场往前爬一步、画一条短线，画进 flow 这块 GPU 位图里，
    // 永远不清（除非按「清空」）——O(1)/帧的积累，Layer 早就扛不住这个量级了。
    Graphics           flow;
    std::vector<Vec2>  flowPts;
    static constexpr int   kFlowW = 1000, kFlowH = 700;
    static constexpr int   kFlowParticles = 4000;
};
State S;

// ---- 1. 万花筒 —— Processing 的 pushMatrix/translate/rotate/scale 循环画同一个形状 ----
// beginShape/vertex/endShape 画花瓣本体，再叠一条 bezier 描边，同一份变换套两种图元。
static const std::vector<Vec2>& petalShape() {
    static const std::vector<Vec2> pts{{0, -4}, {14, -9}, {22, 0}, {14, 9}};
    return pts;
}
static void drawKaleidoscope(Canvas& c) {
    c.camera().fit(Rect(-50, -50, 100, 100));
    const int petals = 10;
    for (int i = 0; i < petals; ++i) {
        c.push();
        c.rotate(S.t * 0.4 + i * 2 * kPi / petals);   // 转起来
        c.translate({20, 0});                          // 挪到花心外
        c.scale(0.7 + 0.3 * std::sin(S.t * 2 + i));     // 一呼一吸地缩放
        c.fill(Color::hsv(i * 360.0 / petals + S.t * 40, 0.75, 0.95, 0.85));
        c.noStroke();
        c.beginShape();
        for (const Vec2& p : petalShape()) c.vertex(p);
        c.endShape(true);
        c.stroke(Color::gray(1.f, 0.6f), 1.5).noFill();
        c.bezier({0, 0}, {10, -12}, {18, 12}, {24, 0});

        // text() vs textWorld()：同一个旋转/缩放的花瓣变换下，text() 的字应该永远
        // 正着朝上不转（只有锚点跟变换），textWorld() 的字应该跟着花瓣一起转/缩
        // （四个角都过矩阵，和 image() 一样）——截图对比这两行字的朝向就能看出来。
        c.fill(Color::gray(0.05f, 0.95f));
        c.textSize(10).text({-6, -16}, "text", Align::Left);
        c.textWorld({-6, 14}, "textWorld", 6, Align::Left);
        c.pop();
    }
}

// ---- 2. 拖尾 —— Scratch 的画笔：Layer 记世界坐标，limit() 让尾巴自动变短 ----
// 切过来先画一条示范曲线：鼠标一动手就会盖过去，但切过来那一刻别是空白画布（D-21）。
static void seedTrailDemo() {
    Vec2 prev{-40, 0};
    for (int i = 1; i <= 60; ++i) {
        Vec2 cur{-40 + i * 80.0 / 60.0, 20 * std::sin(i * 0.25)};
        S.ink.stroke(Color::hsv(i * 6.0, 0.8, 0.95), 3).line(prev, cur);
        prev = cur;
    }
}

// 在 onFrame 里记，不在 onDraw 里记：onDraw 每帧都会重新整个走一遍，记一次就够。
static void updateTrail(App& app) {
    Canvas& c = app.canvas();
    // 这里的 hovered() 是上一帧画布 begin() 时的结果（这一帧的 begin 还没跑到），
    // 鼠标连续移动时差一帧看不出来，对拖尾这种效果无所谓。
    if (!c.hovered()) { S.hasLast = false; return; }
    Vec2 m = c.mouse();
    if (S.hasLast) {
        S.ink.stroke(Color::hsv(std::fmod(S.t * 80, 360.0), 0.8, 0.95), 3).line(S.last, m);
    }
    S.last = m;
    S.hasLast = true;
}

// ---- 3. 噪声地形 —— Processing 的 noise() + map()（Easel 里叫 remap()）：飘动的山脉线 ----
static void drawTerrain(Canvas& c) {
    // 折线端点正好画在 x=0 和 x=100 这两条世界边界上，线宽 2.5 是屏幕像素、不随缩放变化，
    // 世界坐标包围盒量不到它——严格贴合会把线头切掉一截，所以这里手动留 8px。
    c.camera().fit(Rect(0, -40, 100, 80), 8);
    std::vector<Vec2> pts;
    for (double x = 0; x <= 100; x += 2)
        pts.push_back({x, remap(noise(x * 0.05, S.t * 0.3), 0, 1, 20, -20)});
    c.noFill().stroke(Color::hex(0x8D6E63), 2.5);
    c.polyline(pts);
}

// ---- 4. 精灵动画 + 声音 —— Scratch 的「造型」逐帧切换 + 「播放音效」/「响度」 ----
static void drawSpriteAudio(Canvas& c) {
    c.camera().fit(Rect(-40, -40, 80, 40));
    if (!S.sprite) return;                                // 没加载出来就什么都不画，别崩
    int    frame = (int)std::fmod(S.t * 6.0, 4.0);
    double lv = audio::loudness();                         // 放出去的响度，Scratch 的「响度」
    double size = 32 * (1.0 + lv * 0.6);                    // 越响精灵越大
    c.image(S.sprite, Rect::fromCenter({0, -6}, size, size), Rect(frame * 32.0, 0, 32, 32));

    std::vector<float> bands = audio::spectrum(32);         // 一排柱子就是音乐可视化
    c.noStroke();
    for (size_t i = 0; i < bands.size(); ++i) {
        double x = remap((double)i, 0, (double)bands.size(), -35, 35);
        double h = bands[i] * 26.0;
        c.fill(Color::hsv(200 - bands[i] * 160, 0.8, 0.9));
        c.rect(Rect(x, 20 - h, 2.0, h));                    // 从基线向上长，高度是正的
    }
}

// ---- 5. 流场 —— 离屏画布（Graphics）：几千条短线画进一块 GPU 位图，永远不清 ----
// 粒子的初始位置随机撒在缓冲里；出界就换个随机位置重新撒（"回收"），
// 缓冲本身从不清空，所以画面会越积越满，这正是 Graphics 的意义所在。
static void seedFlowField() {
    S.flowPts.resize(State::kFlowParticles);
    for (Vec2& p : S.flowPts) p = {rng().d(0, State::kFlowW), rng().d(0, State::kFlowH)};
}
static void updateFlowField() {
    if (!S.flow.valid()) return;
    Canvas& c = S.flow.begin();          // 之后画的东西都进 flow 这块缓冲（像素坐标）
    const double noiseScale = 0.004;     // 噪声采样密度：越小，流线越"顺"、转弯越慢
    const double stepLen = 2.2;          // 每帧往前爬这么多像素
    for (Vec2& p : S.flowPts) {
        double angle = noise(p.x * noiseScale, p.y * noiseScale, S.t * 0.06) * 2 * kPi * 2;
        Vec2   np = p + Vec2{std::cos(angle), std::sin(angle)} * stepLen;
        c.stroke(Color::hsv(std::fmod(angle * 57.3, 360.0), 0.6, 0.85, 0.5), 1.2).line(p, np);
        p = (np.x < 0 || np.x > State::kFlowW || np.y < 0 || np.y > State::kFlowH)
                ? Vec2{rng().d(0, State::kFlowW), rng().d(0, State::kFlowH)}   // 出界了，换个地方重新撒
                : np;
    }
    S.flow.end();                        // 这一批线这时才真正被烧进纹理
}
static void drawFlowField(Canvas& c) {
    c.camera().fit(Rect(0, 0, State::kFlowW, State::kFlowH));
    c.image(S.flow.texture(), Rect(0, 0, State::kFlowW, State::kFlowH));   // 整块贴出来，O(1)
}

int main(int argc, char** argv) {
    App app(argc, argv);
    app.title("创意包合集").size(1180, 760).theme(Theme::Paper());
    S.part = (int)cli::args().num("part", 0);   // --part 0..4：跳过面板直接看某一节（截图用）
    S.ink.limit(3000);

    app.onStart([] {
        S.sprite = loadTexture("assets/sprite.png", true);   // 只能在这里加载贴图；像素风精灵传 true，放大不糊
        seedTrailDemo();
        // Graphics::create 也只能在这儿（或 onFrame 里）调——要先有显卡上下文。
        if (S.flow.create(State::kFlowW, State::kFlowH)) seedFlowField();
    });

    app.onFrame([&app](double dt) {
        S.t += dt;
        if (S.part == 1) updateTrail(app);
        if (S.part == 4) updateFlowField();   // 每帧往 flow 里烧几千条短线，O(1)，不重放
    });

    app.onDraw([](Canvas& c) {
        switch (S.part) {
            case 0: drawKaleidoscope(c); break;
            case 1: c.draw(S.ink); break;   // 每帧把记下来的笔迹重放一遍
            case 2: drawTerrain(c); break;
            case 3: drawSpriteAudio(c); break;
            default: drawFlowField(c); break;
        }
    });

    app.onClick([](Vec2, Mouse) {
        if (S.part == 3) audio::play("assets/beep.wav");   // Scratch 的「播放音效」
    });

    app.onPanel([&app] {
        if (ui::section("小节")) {
            const char* names[] = {"万花筒", "拖尾", "噪声地形", "精灵动画 + 声音", "流场"};
            ImGui::Combo("切换", &S.part, names, 5);
            if (S.part == 1 && ui::button("清空尾巴", true)) S.ink.clear();
            if (S.part == 2 && ui::button("换一片地形", true)) noiseSeed(++S.seedVal);
            if (S.part == 3) ui::help("点一下画布放个音效，柱子是频谱，精灵会随响度放大。");
            if (S.part == 4) {
                ui::help("离屏画布（Graphics）：几千条短线画进一块 GPU 位图，永远不清，几十万条也不掉帧。");
                if (ui::button("清空", true)) S.flow.clear();
            }
        }
        ui::help("万花筒 = 变换栈；拖尾 = Layer 画笔；地形 = noise()+remap()；精灵 = 造型逐帧；流场 = Graphics 离屏画布。");
        app.status("Easel 创意包 · 五个小节");
    });

    return app.run();
}
