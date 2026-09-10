// Easel — font.cpp  中文字体：先找系统的，再找内置的，都没有就报警告
//
// ImGui 1.92 起字形是按需光栅化的，不用再预先声明「中文字符范围」，
// 所以这里只要找到一个带中文字形的字体文件就行了。
#include "internal.h"

namespace easel {
namespace internal {

namespace {

FontInfo g_info;
ImFont*  g_mono = nullptr;
ImFont*  g_code = nullptr;

// 各平台自带的中文字体，按好看程度排。第一个存在的就用它。
const char* const kCjkFonts[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/msyh.ttc",       // 微软雅黑
    "C:/Windows/Fonts/msyhl.ttc",
    "C:/Windows/Fonts/Deng.ttf",       // 等线
    "C:/Windows/Fonts/simhei.ttf",     // 黑体
    "C:/Windows/Fonts/simsun.ttc",     // 宋体（一定有）
#elif defined(__APPLE__)
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/Supplemental/Songti.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
#else
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/arphic/uming.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    nullptr};

const char* const kMonoFonts[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Monaco.ttf",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
#endif
    nullptr};

// 内置兜底字体（思源黑体子集）。仓库里有就用，没有也不影响运行。
const char* const kBundled[] = {"assets/fonts/NotoSansSC-subset.otf",
                                "assets/fonts/NotoSansSC-subset.ttf",
                                "../assets/fonts/NotoSansSC-subset.otf", nullptr};

}  // namespace

const FontInfo& fontInfo() { return g_info; }
ImFont*         monoFont() { return g_mono ? g_mono : ImGui::GetFont(); }
ImFont*         codeFont() { return g_code ? g_code : monoFont(); }

void buildFonts(const Theme& theme, float dpiScale) {
    ImGuiIO&    io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    io.Fonts->Clear();
    g_mono = nullptr;
    g_info = FontInfo{};
    g_info.sizePx = theme.fontSize;
    style.FontSizeBase = theme.fontSize;

    struct Candidate {
        std::string path, source;
    };
    std::vector<Candidate> list;

    if (!theme.fontPath.empty()) list.push_back({theme.fontPath, "主题指定"});
    if (const char* env = std::getenv("EASEL_FONT")) list.push_back({env, "环境变量 EASEL_FONT"});
    for (const char* const* p = kBundled; *p; ++p) list.push_back({*p, "内置子集"});
    for (const char* const* p = kCjkFonts; *p; ++p) list.push_back({*p, "系统字体"});

    ImFont* font = nullptr;
    for (const Candidate& c : list) {
        if (!fs::exists(c.path)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        std::snprintf(cfg.Name, sizeof cfg.Name, "%s", fs::dirOf(c.path).empty() ? c.path.c_str()
                                                                                 : c.path.c_str());
        font = io.Fonts->AddFontFromFileTTF(c.path.c_str(), 0.0f, &cfg);
        if (font) {
            g_info.path = c.path;
            g_info.source = c.source;
            break;
        }
    }

    if (!font) {
        io.Fonts->AddFontDefaultVector();
        g_info.source = "ImGui 默认字体";
        g_info.note = "没找到任何中文字体，界面里的中文会显示成方框。把字体路径写进 "
                      "Theme::fontPath，或者设环境变量 EASEL_FONT。";
        EASEL_WARN("%s", g_info.note.c_str());
    } else {
        // 真的问一句：这个字体有没有「中」字？
        ImGui::GetIO().Fonts->Build();
        g_info.cjk = font->IsGlyphInFont((ImWchar)0x4E2D);
        if (!g_info.cjk) {
            g_info.note = "选中的字体 " + g_info.path + " 里没有中文字形。";
            EASEL_WARN("%s", g_info.note.c_str());
        }
    }

    // 日志窗和调试台用等宽字体（对齐好看），但等宽字体基本都没有中文，
    // 所以把上面那个中文字体合并进来做兜底 —— 否则日志里的中文会变成方框。
    for (const char* const* p = kMonoFonts; *p; ++p) {
        if (!fs::exists(*p)) continue;
        g_mono = io.Fonts->AddFontFromFileTTF(*p, 0.0f);
        if (!g_mono) continue;
        if (!g_info.path.empty()) {
            ImFontConfig merge;
            merge.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(g_info.path.c_str(), 0.0f, &merge);
        }
        break;
    }

    // 编辑栏专用的「代码字体」。和日志窗那个不一样，多做了一件事：
    // ImGuiColorTextEdit 是按固定列宽画的 —— 每个码点占一列，列宽 = '#' 的宽度。
    // 直接把中文合进来，一个汉字有两列那么宽，就会糊成一团（Mac 上实测过）。
    // 所以中文按列宽缩小合并：一个汉字正好一列，光标、选中、点击位置也就全对了。
    //
    // 关键是这两个比例都得**量**出来，不能拍脑袋：
    // ImGui 的「字号」是行高（ascent+descent），不是 em。Menlo 的一列只有 0.52 字高，
    // 中文字却有 0.85 字高。早先按 0.62 硬撑列宽，等于给英文加了 20% 字距，
    // 代码看着就像打字机 —— 那是错的，列宽必须保持字体天然的样子。
    if (g_mono && font) {
        io.Fonts->Build();
        ImFontBaked* mb = g_mono->GetFontBaked(theme.fontSize);
        ImFontBaked* cb = font->GetFontBaked(theme.fontSize);
        float col = mb ? mb->GetCharAdvance('#') / theme.fontSize : 0.f;
        float cjk = cb ? cb->GetCharAdvance((ImWchar)0x4E2D) / theme.fontSize : 0.f;
        g_info.colRatio = col;
        if (col > 0.2f && col < 1.2f && cjk > 0.3f) {
            for (const char* const* p = kMonoFonts; *p; ++p) {
                if (!fs::exists(*p)) continue;
                g_code = io.Fonts->AddFontFromFileTTF(*p, 0.0f);   // 列宽保持天然
                if (!g_code) continue;
                g_info.monoPath = *p;
                ImFontConfig merge;
                merge.MergeMode = true;
                merge.ExtraSizeScale = (col / cjk) * 0.98f;   // 一个汉字正好一列，留 2% 的缝
                io.Fonts->AddFontFromFileTTF(g_info.path.c_str(), 0.0f, &merge);
                io.Fonts->Build();
                break;
            }
        }
    }

    EASEL_UNUSED(dpiScale);
}

}  // namespace internal
}  // namespace easel
