// Easel — debug_console.cpp
// F12 调试台。按学生卡住时会问的问题分页（D-24）：
//   日志 —— 我打的东西去哪了       追踪 —— 这个数怎么变的
//   状态 —— 现在到底是什么         画布 —— 为什么什么都没画出来
//   用例 —— 怎么把问题带回单文件   自检 —— 环境有没有问题
#include "internal.h"

namespace easel {
namespace internal {

namespace {

const char* levelLabel(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "追踪";
        case LogLevel::Info: return "信息";
        case LogLevel::Warn: return "警告";
        case LogLevel::Error: return "错误";
        case LogLevel::Check: return "断言";
    }
    return "?";
}

ImVec4 levelColor(LogLevel l, const Theme& th) {
    switch (l) {
        case LogLevel::Warn: return iv4(th.warn);
        case LogLevel::Error:
        case LogLevel::Check: return iv4(th.bad);
        default: return iv4(th.fg);
    }
}

// ------------------------------------------------------------------ 日志
void tabLog(const Theme& th) {
    Shared& s = shared();
    ImGui::SetNextItemWidth(150 * s.dpi);
    const char* levels[] = {"全部", "信息以上", "警告以上", "只看错误"};
    ImGui::Combo("级别", &s.logLevelFilter, levels, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220 * s.dpi);
    ImGui::InputTextWithHint("##filter", "过滤文字…", s.logFilter, sizeof s.logFilter);
    ImGui::SameLine();
    ImGui::Checkbox("跟随最新", &s.logAutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("清空")) { s.log.clear(); s.droppedLogs = 0; }
    ImGui::SameLine();
    if (ImGui::Button("复制全部")) {
        std::string all;
        for (const LogEntry& e : s.log) {
            all += "[" + std::string(levelLabel(e.level)) + "] " + e.file + ":" +
                   std::to_string(e.line) + "  " + e.msg + "\n";
        }
        ImGui::SetClipboardText(all.c_str());
    }
    if (s.droppedLogs > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("（已丢弃最早的 %d 条）", s.droppedLogs);
    }
    ImGui::Separator();

    ImGui::BeginChild("##loglist", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * 0.94f);
    int minLevel = s.logLevelFilter == 0   ? (int)LogLevel::Trace
                   : s.logLevelFilter == 1 ? (int)LogLevel::Info
                   : s.logLevelFilter == 2 ? (int)LogLevel::Warn
                                           : (int)LogLevel::Error;
    for (const LogEntry& e : s.log) {
        if ((int)e.level < minLevel) continue;
        if (s.logFilter[0] && e.msg.find(s.logFilter) == std::string::npos &&
            e.file.find(s.logFilter) == std::string::npos)
            continue;
        ImGui::PushStyleColor(ImGuiCol_Text, levelColor(e.level, th));
        if (e.file == "cout")
            ImGui::Text("f%-5lld  %s", e.frame, e.msg.c_str());
        else
            ImGui::Text("f%-5lld [%s] %s:%d  %s", e.frame, levelLabel(e.level), e.file.c_str(),
                        e.line, e.msg.c_str());
        ImGui::PopStyleColor();
        if (e.repeat > 1) {
            ImGui::SameLine();
            ImGui::TextDisabled("  x%d", e.repeat);
        }
    }
    ImGui::PopFont();
    if (s.logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
}

// ------------------------------------------------------------------ 追踪
void tabTrace(const Theme& th) {
    Shared& s = shared();
    if (s.traces.empty()) {
        ui::help("还没有数据。在算法里写一行：\n\n    EASEL_TRACE(\"路线长度\", cur.length);\n\n"
                 "在命令行里它会打印成 CSV，在这里它会自己变成折线图。同一行代码，两边都能用。");
        return;
    }
    if (ImGui::Button("清空")) s.traces.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("共 %d 条曲线", (int)s.traces.size());
    ImGui::Separator();
    for (TraceSeries& t : s.traces) {
        std::string title = t.name + "  （" + std::to_string(t.ys.size()) + " 个点）";
        if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ui::chart(("##plot" + t.name).c_str(), t.xs, t.ys, t.name.c_str(), 170.f * s.dpi);
            if (!t.ys.empty()) {
                double lo = *std::min_element(t.ys.begin(), t.ys.end());
                double hi = *std::max_element(t.ys.begin(), t.ys.end());
                ImGui::TextDisabled("最新 %.6g   最小 %.6g   最大 %.6g", t.ys.back(), lo, hi);
            }
            ImGui::TreePop();
        }
    }
    EASEL_UNUSED(th);
}

// ------------------------------------------------------------------ 状态
void tabState() {
    Shared& s = shared();
    if (s.dbgs.empty()) {
        ui::help("还没有东西。在任何地方写：\n\n    easel::dbg(\"当前长度\", cur.length);\n\n"
                 "它在命令行下什么都不做（不刷屏），在这里会实时显示。");
        return;
    }
    ImGui::TextDisabled("第 %lld 帧", s.frame);
    ImGui::SameLine();
    if (ImGui::Button("清空")) s.dbgs.clear();
    ImGui::Separator();
    if (ImGui::BeginTable("##dbg", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("名字", ImGuiTableColumnFlags_WidthFixed, 160 * s.dpi);
        ImGui::TableSetupColumn("值");
        ImGui::TableSetupColumn("变化", ImGuiTableColumnFlags_WidthFixed, 150 * s.dpi);
        ImGui::TableHeadersRow();
        for (const DbgValue& d : s.dbgs) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.key.c_str());
            ImGui::TableNextColumn();
            if (d.frame < s.frame - 1) ImGui::TextDisabled("%s", d.value.c_str());
            else ImGui::TextUnformatted(d.value.c_str());
            ImGui::TableNextColumn();
            if (d.numeric && d.history.size() > 1) {
                std::vector<float> f(d.history.begin(), d.history.end());
                ImGui::PlotLines(("##sp" + d.key).c_str(), f.data(), (int)f.size(), 0, nullptr,
                                 FLT_MAX, FLT_MAX, ImVec2(-1, 28 * s.dpi));
            } else {
                ImGui::TextDisabled("—");
            }
        }
        ImGui::EndTable();
    }
}

// ------------------------------------------------------------------ 画布
void tabCanvas(const Theme& th) {
    Shared& s = shared();
    if (!s.canvas || !s.camera) return;
    const Canvas::Stats& st = s.canvas->stats();
    Camera&              cam = *s.camera;

    auto row = [&](const char* k, const std::string& v, bool bad = false) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(k);
        ImGui::TableNextColumn();
        if (bad) ImGui::TextColored(iv4(th.warn), "%s", v.c_str());
        else ImGui::TextUnformatted(v.c_str());
    };

    ui::title("这一帧画了什么");
    if (ImGui::BeginTable("##diag", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 230 * s.dpi);
        ImGui::TableSetupColumn("v");
        row("onDraw 接上了吗", s.hasOnDraw ? "接上了" : "没有！app.onDraw(...) 没调用", !s.hasOnDraw);
        row("图元总数", std::to_string(st.primitives), st.primitives == 0 && s.hasOnDraw);
        row("跑到视口外的", std::to_string(st.offscreen), st.offscreen > 0 && st.offscreen == st.primitives);
        row("画了但看不见的", std::to_string(st.invisible) + "（线宽或透明度为 0）", st.invisible > 0);
        row("线/圆/矩形/文字/图片",
            std::to_string(st.lines) + " / " + std::to_string(st.circles) + " / " +
                std::to_string(st.rects) + " / " + std::to_string(st.texts) + " / " +
                std::to_string(st.images));
        Rect vw = cam.visibleWorld();
        char buf[192];
        std::snprintf(buf, sizeof buf, "x %.2f … %.2f    y %.2f … %.2f", vw.left(), vw.right(),
                      vw.top(), vw.bottom());
        row("看得见的世界范围", buf);
        std::snprintf(buf, sizeof buf, "%.4f 像素 / 单位", cam.zoom());
        row("缩放", buf);
        Vec2 c = cam.center();
        std::snprintf(buf, sizeof buf, "(%.2f, %.2f)", c.x, c.y);
        row("相机中心", buf);
        Vec2 m = cam.toWorld(ev(ImGui::GetIO().MousePos));
        std::snprintf(buf, sizeof buf, "(%.2f, %.2f)", m.x, m.y);
        row("鼠标（世界坐标）", buf);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (st.primitives == 0 && s.hasOnDraw)
        ui::help("一个图元都没有：onDraw 里可能提前 return 了，或者数据还是空的。");
    else if (st.primitives > 0 && st.offscreen == st.primitives)
        ui::help("所有东西都画在视口外了。多半是坐标范围和相机对不上——"
                 "试试 app.camera().fit(easel::Rect::bounding(所有点))。");
    ImGui::Spacing();
    if (ImGui::Button("把相机重置")) { cam.reset(); }
    ImGui::SameLine();
    ui::help("（回到原点、缩放 1.0）");
}

// ------------------------------------------------------------------ 用例
void tabCases(const Theme& th) {
    Shared&            s = shared();
    static char        note[256] = {0};
    static std::string lastPath;

    ui::title("把 GUI 里的问题带回单文件");
    ui::help("导出 = 把此刻的数据和随机种子写成一个文件，再给你一行命令行。"
             "在终端里复现、修好、跑测试，然后回到这里。");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##note", "给这个用例写一句话（可选）：比如「点第 7 个站点就飞了」", note,
                             sizeof note);
    ImGui::Spacing();
    bool can = s.exportProvider && *s.exportProvider;
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("导出调试用例", ImVec2(-FLT_MIN, 34 * s.dpi))) {
        json state = (*s.exportProvider)();
        lastPath = debug::exportCase(state, note);
        note[0] = 0;
    }
    ImGui::EndDisabled();
    if (!can)
        ImGui::TextColored(iv4(th.warn),
                           "还不能导出：main 里要写 app.onExportCase([]{ return json(S); });");
    if (!lastPath.empty()) {
        ImGui::Spacing();
        ImGui::Text("最近导出：%s", lastPath.c_str());
        if (ImGui::Button("复制复现命令")) {
            std::string cmd = cli::args().program + " --case " + lastPath + " --seed " +
                              std::to_string(current_seed());
            ImGui::SetClipboardText(cmd.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ui::title("debug/ 目录里已有的用例");

    // 扫目录是文件操作，别每帧都干；两秒刷一次，或者手动刷。
    static std::vector<std::pair<std::string, bool>> files;   // 路径 + 是不是对拍抓到的
    static double                                    nextScan = 0;
    if (ImGui::SmallButton("刷新")) nextScan = 0;
    if (ImGui::GetTime() >= nextScan) {
        nextScan = ImGui::GetTime() + 2.0;
        files.clear();
        for (int i = 1; i < 200; ++i) {
            std::string f = debug::caseFile(i);
            if (fs::exists(f)) files.emplace_back(f, false);
        }
        for (int i = 1; i < 200; ++i) {
            char b[64];
            std::snprintf(b, sizeof b, "/duipai-%03d.json", i);
            std::string f = debug::dir() + b;
            if (fs::exists(f)) files.emplace_back(f, true);
        }
    }
    for (const auto& kv : files) {
        ImGui::BulletText("%s%s", kv.first.c_str(), kv.second ? "  （对拍抓到的）" : "");
        if (!kv.second) {
            ImGui::SameLine();
            std::string bid = "复制命令##" + kv.first;
            if (ImGui::SmallButton(bid.c_str()))
                ImGui::SetClipboardText((cli::args().program + " --case " + kv.first).c_str());
        }
    }
    if (files.empty()) ImGui::TextDisabled("（还没有。debug/ 目录就是你的 bug 日志。）");
}

// ------------------------------------------------------------------ 自检
void tabDoctor(App& app) {
    static std::string cached;
    Shared&            s = shared();
    if (cached.empty() || ImGui::Button("重新检查")) cached = app.doctor();
    ImGui::SameLine();
    if (ImGui::Button("复制")) ImGui::SetClipboardText(cached.c_str());
    ImGui::SameLine();
    ui::help("（命令行里也能看：app --doctor）");

    // 帧率：整数，每 0.5 秒才刷新一次显示值 —— 原始帧率每帧都在跳，看着晃眼
    static double lastUpdate = 0;
    static int    shown = 0;
    double        now = ImGui::GetTime();
    if (now - lastUpdate >= 0.5) {
        lastUpdate = now;
        shown = (int)(ImGui::GetIO().Framerate + 0.5f);
    }
    ImGui::Text("帧率：%d FPS", shown);

    ImGui::Separator();
    ImGui::PushFont(monoFont(), ImGui::GetStyle().FontSizeBase * 0.94f);
    ImGui::TextUnformatted(cached.c_str());
    ImGui::PopFont();
    if (s.crashed) {
        ImGui::Separator();
        ui::title("上次崩溃");
        ImGui::TextWrapped("%s", s.crashWhat.c_str());
        ImGui::TextUnformatted(s.crashStack.c_str());
    }
}

// ------------------------------------------------------------------ 帮助
void tabHelp() {
    ui::title("Easel 没有的东西，直接调 ImGui");
    ui::help("easel::ui 只是一层薄词汇，不是墙。#include <imgui.h> 永远可用，"
             "两者可以写在同一个函数里。下面这两个示例窗口就是活的参考手册：看到想要的效果，"
             "点开源码就知道怎么写。");
    ImGui::Spacing();
    static bool demo = false, pdemo = false;
    ImGui::Checkbox("打开 ImGui 示例窗口（控件大全）", &demo);
    ImGui::Checkbox("打开 ImPlot 示例窗口（图表大全）", &pdemo);
    if (demo) ImGui::ShowDemoWindow(&demo);
    if (pdemo) ImPlot::ShowDemoWindow(&pdemo);
    ImGui::Spacing();
    ImGui::Separator();
    ui::title("键盘 / 鼠标");
    ImGui::BulletText("滚轮：以鼠标为中心缩放");
    ImGui::BulletText("中键拖拽，或按住空格 + 左键拖拽：平移");
    ImGui::BulletText("F12：开关这个调试台");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Easel " EASEL_VERSION " · 代码酷 daimaku.net · MIT");
}

}  // namespace

// ============================================================ 断言横幅
void drawCheckBanner(App& app) {
    Shared& s = shared();
    if (!s.banner) return;
    const Theme&   th = app.theme();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float          dpi = s.dpi;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 16 * dpi, vp->WorkPos.y + 16 * dpi));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.62f, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, iv4(th.bad.darker(0.35f)));
    ImGui::Begin("##banner", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::Text("EASEL_CHECK 失败  ·  %s", s.bannerWhere.c_str());
    ImGui::PopFont();
    ImGui::TextWrapped("%s", s.bannerMsg.c_str());
    ImGui::TextDisabled("%s", s.bannerExpr.c_str());
    ImGui::Spacing();
    if (ImGui::Button("知道了")) s.banner = false;
    ImGui::SameLine();
    if (ImGui::Button("打开调试台")) { s.banner = false; app.debugConsoleOpen(true); }
    ImGui::SameLine();
    ImGui::TextDisabled("（程序没有崩，回放已经暂停）");
    ImGui::End();
    ImGui::PopStyleColor();

    if (s.timeline) s.timeline->pause();
}

// ============================================================ 调试台
void drawDebugConsole(bool* open, App& app) {
    const Theme& th = app.theme();
    Shared&      s = shared();
    ImGui::SetNextWindowSize(ImVec2(760 * s.dpi, 460 * s.dpi), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60 * s.dpi, 60 * s.dpi), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("调试台 (F12)", open)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##dbgtabs")) {
        if (ImGui::BeginTabItem("日志")) { tabLog(th); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("追踪")) { tabTrace(th); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("状态")) { tabState(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("画布")) { tabCanvas(th); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("用例")) { tabCases(th); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("自检")) { tabDoctor(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("帮助")) { tabHelp(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace internal
}  // namespace easel
