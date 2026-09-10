// ============================================================================
//  Easel — core.h
//  「画架」算法可视化调试库 · 代码酷 daimaku.net · MIT
//
//  这个头文件里没有任何图形界面代码，也不需要链接任何库。
//  它可以被一条命令编译：
//
//      g++ -std=c++17 -DEASEL_STANDALONE solver.cpp && ./a.out
//
//  同一份 solver.cpp 有三个入口：
//      CLI   g++ -DEASEL_STANDALONE solver.cpp     算法开发（OI 的方式）
//      Test  tests/test_solver.cpp   #include 它 + doctest
//      App   src/app.cpp             #include 它 + Easel 图形界面
//
//  在 App 里，下面这些宏会自动接到界面上：
//      EASEL_LOG    -> 应用内日志窗
//      EASEL_TRACE  -> 自动折线图
//      EASEL_CHECK  -> 红色横幅（不崩溃）
//      easel::dbg   -> F12 调试台的「状态」页
//  在 CLI 里，它们就是 printf。**同一行代码，两个世界。**
// ============================================================================
#ifndef EASEL_CORE_H
#define EASEL_CORE_H

#define EASEL_VERSION       "0.1.0"
#define EASEL_VERSION_MAJOR 0
#define EASEL_VERSION_MINOR 1
#define EASEL_VERSION_PATCH 0

// ---------------------------------------------------------------- C++ 版本
#if defined(_MSC_VER) && !defined(__clang__)
#  if _MSVC_LANG < 201703L
#    error "Easel 需要 C++17：MSVC 请加 /std:c++17"
#  endif
#elif __cplusplus < 201703L
#  error "Easel 需要 C++17：编译时加 -std=c++17"
#endif

// ---------------------------------------------------------------- 标准库
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <functional>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 平台
#if defined(_WIN32)
#  include <direct.h>
#  include <io.h>
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // cli::parse() 靠它拿 UTF-16 命令行；崩溃捕获（下面）也用得上
#  include <shellapi.h>  // CommandLineToArgvW
#  if defined(_MSC_VER)
#    pragma comment(lib, "shell32.lib")   // CommandLineToArgvW；MinGW 靠 CMakeLists.txt 里显式链 shell32
#  endif
#  define EASEL_MKDIR(p) ::_mkdir(p)
#  define EASEL_GETCWD(b, n) ::_getcwd((b), (n))
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  ifndef EASEL_NO_STACKTRACE
#    include <execinfo.h>
#  endif
#  define EASEL_MKDIR(p) ::mkdir((p), 0755)
#  define EASEL_GETCWD(b, n) ::getcwd((b), (n))
#endif

// ---------------------------------------------------------------- nlohmann/json
// amalgamate.py 会把 json.hpp 的内容替换到下面这一行的位置（stb 风格单文件）。
#ifndef EASEL_AMALGAMATED
#  include <nlohmann/json.hpp>
#endif

// ---------------------------------------------------------------- 小工具宏
#if defined(_MSC_VER)
#  define EASEL_DEBUG_BREAK() __debugbreak()
#  define EASEL_PRINTF_FMT(a, b)
#else
#  define EASEL_DEBUG_BREAK() __builtin_trap()
#  define EASEL_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#endif

#define EASEL_UNUSED(x) (void)(x)

namespace easel {

using json = nlohmann::json;

// 给自己的结构体加上 JSON 读写能力（存档、导出调试用例、F12 状态页都靠它）：
//     struct Station { std::string name; Vec2 pos; int trash = 0; };
//     EASEL_JSON(Station, name, pos, trash)
#define EASEL_JSON(Type, ...) NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__)
// 私有成员用这个，写在 struct/class 内部。
#define EASEL_JSON_MEMBER(Type, ...) NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__)

// ============================================================================
//  1. 几何：Vec2 / Rect / Color
// ============================================================================

struct Vec2 {
    double x = 0, y = 0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2  operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2  operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2  operator*(double s) const { return {x * s, y * s}; }
    Vec2  operator/(double s) const { return {x / s, y / s}; }
    Vec2  operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    bool  operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool  operator!=(const Vec2& o) const { return !(*this == o); }

    double length() const { return std::sqrt(x * x + y * y); }
    double length2() const { return x * x + y * y; }
    Vec2   normalized() const { double L = length(); return L > 1e-12 ? Vec2{x / L, y / L} : Vec2{0, 0}; }
    Vec2   perp() const { return {-y, x}; }                       // 逆时针 90°
    double angle() const { return std::atan2(y, x); }
    Vec2   rotated(double rad) const {
        double c = std::cos(rad), s = std::sin(rad);
        return {x * c - y * s, x * s + y * c};
    }
};

inline Vec2   operator*(double s, const Vec2& v) { return {v.x * s, v.y * s}; }
inline double dist(const Vec2& a, const Vec2& b) { return (a - b).length(); }
inline double dist2(const Vec2& a, const Vec2& b) { return (a - b).length2(); }
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline Vec2   lerp(const Vec2& a, const Vec2& b, double t) { return a + (b - a) * t; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
// 普通重载而不是模板：C++17 的 <algorithm> 也有个 std::clamp<T>，两者都在作用域里时
// （`using namespace std; using namespace easel;`）重载决议照样只挑这个非模板版本，
// 不会像当年的 map()/std::map 那样撞名——这也是它没有像旧版本那样叫 clampd 的原因。
inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 把 v 从 [lo,hi] 这段范围搬到 [lo2,hi2] 上（对应 Processing 的 map()；叫 remap
// 是因为 map 和 std::map 这个关联容器模板撞名）。
// 学生最常用它把「数据」变成「画面」：温度 -> 颜色、下标 -> 屏幕位置、噪声 -> 半径。
//     double r = remap(t, 0, 1, 5, 50);      // t 从 0 涨到 1，半径从 5 涨到 50
// 不夹紧：v 超出 [lo,hi] 时结果也会超出 [lo2,hi2]（要夹就再套一层 clamp）。
// lo == hi 时没有唯一答案，返回 lo2，避免除零得到 inf/nan。
inline double remap(double v, double lo, double hi, double lo2, double hi2) {
    if (std::fabs(hi - lo) < 1e-15) return lo2;
    return lo2 + (v - lo) * (hi2 - lo2) / (hi - lo);
}

inline std::ostream& operator<<(std::ostream& os, const Vec2& v) {
    return os << '(' << v.x << ", " << v.y << ')';
}
inline void to_json(json& j, const Vec2& v) { j = json::array({v.x, v.y}); }
inline void from_json(const json& j, Vec2& v) {
    if (j.is_array() && j.size() >= 2) { v.x = j[0].get<double>(); v.y = j[1].get<double>(); }
    else if (j.is_object()) { v.x = j.value("x", 0.0); v.y = j.value("y", 0.0); }
}

// 轴对齐矩形。x/y 是左上角（屏幕坐标习惯，世界坐标也用同一套）。
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;

    Rect() = default;
    Rect(double x_, double y_, double w_, double h_) : x(x_), y(y_), w(w_), h(h_) {}

    static Rect fromCorners(const Vec2& a, const Vec2& b) {
        double x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        return {x0, y0, std::fabs(a.x - b.x), std::fabs(a.y - b.y)};
    }
    static Rect fromCenter(const Vec2& c, double w_, double h_) {
        return {c.x - w_ / 2, c.y - h_ / 2, w_, h_};
    }
    // 包住一堆点（常用来给 camera.fit 算范围）
    static Rect bounding(const std::vector<Vec2>& pts) {
        if (pts.empty()) return {};
        Vec2 lo = pts[0], hi = pts[0];
        for (const Vec2& p : pts) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
        return fromCorners(lo, hi);
    }

    double left() const { return x; }
    double right() const { return x + w; }
    double top() const { return y; }
    double bottom() const { return y + h; }
    Vec2   min() const { return {x, y}; }
    Vec2   max() const { return {x + w, y + h}; }
    Vec2   center() const { return {x + w / 2, y + h / 2}; }
    bool   empty() const { return w <= 0 || h <= 0; }

    bool contains(const Vec2& p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
    bool overlaps(const Rect& o) const {
        return !(o.x > right() || o.right() < x || o.y > bottom() || o.bottom() < y);
    }
    Rect expanded(double m) const { return {x - m, y - m, w + 2 * m, h + 2 * m}; }
    Rect united(const Rect& o) const {
        if (empty()) return o;
        if (o.empty()) return *this;
        double x0 = std::min(x, o.x), y0 = std::min(y, o.y);
        double x1 = std::max(right(), o.right()), y1 = std::max(bottom(), o.bottom());
        return {x0, y0, x1 - x0, y1 - y0};
    }
};

inline std::ostream& operator<<(std::ostream& os, const Rect& r) {
    return os << "Rect(" << r.x << ", " << r.y << ", " << r.w << " x " << r.h << ')';
}
inline void to_json(json& j, const Rect& r) { j = json::array({r.x, r.y, r.w, r.h}); }
inline void from_json(const json& j, Rect& r) {
    if (j.is_array() && j.size() >= 4) {
        r.x = j[0].get<double>(); r.y = j[1].get<double>();
        r.w = j[2].get<double>(); r.h = j[3].get<double>();
    }
}

// 颜色，分量 0..1。
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    // Color::hex(0x2E7D32) —— 和 CSS / 设计稿里的写法一致
    static Color hex(unsigned int rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    static Color rgb(int R, int G, int B, int A = 255) {
        return {R / 255.f, G / 255.f, B / 255.f, A / 255.f};
    }
    // h 单位是度（0..360），s / v 是 0..1
    static Color hsv(double h, double s, double v, float alpha = 1.f) {
        h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 60.0;
        int    i = (int)h;
        double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        double R = v, G = t, B = p;
        switch (i) {
            case 1: R = q; G = v; B = p; break;
            case 2: R = p; G = v; B = t; break;
            case 3: R = p; G = q; B = v; break;
            case 4: R = t; G = p; B = v; break;
            case 5: R = v; G = p; B = q; break;
            default: break;
        }
        return {(float)R, (float)G, (float)B, alpha};
    }
    static Color gray(float v, float alpha = 1.f) { return {v, v, v, alpha}; }

    Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    Color lighter(float t = 0.2f) const {
        return {r + (1 - r) * t, g + (1 - g) * t, b + (1 - b) * t, a};
    }
    Color darker(float t = 0.2f) const { return {r * (1 - t), g * (1 - t), b * (1 - t), a}; }
    Color mix(const Color& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
    // 0xAABBGGRR —— ImGui 的 IM_COL32 布局。core.h 不认识 ImGui，只给出这个数。
    std::uint32_t rgba32() const {
        auto q = [](float v) -> std::uint32_t {
            int i = (int)(clamp(v, 0.0, 1.0) * 255.0 + 0.5);
            return (std::uint32_t)i;
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

inline void to_json(json& j, const Color& c) { j = json::array({c.r, c.g, c.b, c.a}); }
inline void from_json(const json& j, Color& c) {
    if (j.is_array() && j.size() >= 3) {
        c.r = j[0].get<float>(); c.g = j[1].get<float>(); c.b = j[2].get<float>();
        c.a = j.size() > 3 ? j[3].get<float>() : 1.f;
    } else if (j.is_string()) {
        std::string s = j.get<std::string>();
        if (!s.empty() && s[0] == '#') s.erase(0, 1);
        c = Color::hex((unsigned int)std::strtoul(s.c_str(), nullptr, 16));
    }
}

// ============================================================================
//  2. 把任意值变成字符串（EASEL_CHECK_EQ、dbg、对拍器都要用）
// ============================================================================
namespace detail {

template <class T, class = void>
struct HasOstream : std::false_type {};
template <class T>
struct HasOstream<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T, class = void>
struct HasJson : std::false_type {};
template <class T>
struct HasJson<T, std::void_t<decltype(json(std::declval<const T&>()))>> : std::true_type {};

template <class T>
std::string stringify(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (HasOstream<T>::value) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else if constexpr (HasJson<T>::value) {
        return json(v).dump();
    } else {
        return "<不可打印>";
    }
}
inline std::string stringify(const char* v) { return v ? v : "(null)"; }

}  // namespace detail

// ============================================================================
//  3. 随机数：库默认每次运行都不一样，但种子永远可见、永远可复现（D-34，修正 D-19）
//     算法类工程（template）在 main 里显式 seed(EASEL_FIXED_SEED) 把它钉死。
// ============================================================================
// 算法类工程显式固定种子时用的值（`template/src/solver.cpp`、`duipai::run` 的默认起点都用它）。
// 只是「钉死用的锚点」，不是库的运行时默认——库的运行时默认是随机的，见下面 Rng/rng()。
#ifndef EASEL_FIXED_SEED
#  define EASEL_FIXED_SEED 20260101u
#endif

namespace detail {
// 运行时种子：优先用 std::random_device（大多数平台是真随机），拿不到熵源就退回当前时间。
// 只真正生成一次（`static` 局部变量），全局 rng() 和 noise() 的初始种子共用这同一个值——
// 此后谁调 seed()/noiseSeed() 才会分开走。
inline unsigned runtimeSeed() {
    std::random_device rd;
    if (rd.entropy() > 0) return (unsigned)rd();
    return (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count();
}
inline unsigned initialSeed() {
    static unsigned s = runtimeSeed();
    return s;
}
}  // namespace detail

class Rng {
public:
    // 类自己的默认构造也是运行时随机种子——和全局 rng() 的默认行为保持一致（D-35）。
    // 想要「同一条命令永远同一个结果」，显式传 EASEL_FIXED_SEED 或任何你自己钉死的值。
    explicit Rng(unsigned seed = detail::initialSeed()) : g_(seed), seed_(seed) {}

    void     reseed(unsigned seed) { g_.seed(seed); seed_ = seed; }
    unsigned seed() const { return seed_; }

    int    i(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_); }  // 闭区间
    double d(double lo = 0.0, double hi = 1.0) { return std::uniform_real_distribution<double>(lo, hi)(g_); }
    bool   chance(double p) { return d() < p; }
    double normal(double mean = 0.0, double sd = 1.0) { return std::normal_distribution<double>(mean, sd)(g_); }

    template <class T> void shuffle(std::vector<T>& v) { std::shuffle(v.begin(), v.end(), g_); }
    template <class T> const T& pick(const std::vector<T>& v) { return v[(size_t)i(0, (int)v.size() - 1)]; }

    std::mt19937& engine() { return g_; }

private:
    std::mt19937 g_;
    unsigned     seed_;
};

// 全局随机源。不加 `--seed` 时每次运行都是一个新种子（`detail::initialSeed()`）；
// 加了 `--seed N` 就用 N。种子本身永远由 `cli::parse` 打一行日志，做到「不一样但看得见」。
inline Rng& rng() {
    static Rng r(detail::initialSeed());
    return r;
}
inline void noiseSeed(unsigned s);   // 定义见下面「噪声」一节；seed() 要连带把它也改掉
inline void     seed(unsigned s) { rng().reseed(s); noiseSeed(s); }
inline unsigned current_seed() { return rng().seed(); }

// ---------------------------------------------------------------- 噪声 noise()
// Processing 的 noise()，给「画面」用的随机：rng().d() 每次跳得到处都是，
// noise() 相邻的输入给相邻的输出 —— 所以它画出来是山脉、云、飘动的草，而不是雪花点。
//     for (int i = 0; i < 200; ++i)
//         c.dot({i * 0.5, remap(noise(i * 0.05, t), 0, 1, -5, 5)});   // 一条起伏的地形线
// 一维当时间，二维当地形/云，三维常用来做「会动的二维图案」（第三维喂时间）。
// 返回值永远在 [0,1]，中间值最多。同一个种子永远画出同一片山（D-34）；
// 不显式调 noiseSeed() 的话，它跟着全局的 seed()/current_seed() 走。
namespace detail {

// Ken Perlin 2002「改进版」的三件套：fade 让格子边界的一阶二阶导都连续，
// grad 用 12 个固定方向代替查表梯度，lerp 就是线性插值。
inline double noiseFade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline double noiseGrad(int hash, double x, double y, double z) {
    int    h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

// 置换表 + 倍频参数。放在一个函数里的 static，头文件才能只有声明没有定义。
struct NoiseState {
    int    perm[512];
    int    octaves = 4;      // 叠几层。层越多细节越碎
    double falloff = 0.5;    // 每层的音量。越小越平滑

    NoiseState() { reseed(initialSeed()); }   // 和 rng() 初始用的是同一颗运行时种子

    void reseed(unsigned s) {
        for (int i = 0; i < 256; ++i) perm[i] = i;
        std::mt19937 g(s);
        std::shuffle(perm, perm + 256, g);
        for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
    }
};
inline NoiseState& noiseState() {
    static NoiseState st;
    return st;
}

// 单层 Perlin，返回大约 [-1,1]。
inline double perlin1(double x, double y, double z) {
    const NoiseState& s = noiseState();
    int    X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255, Z = (int)std::floor(z) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    double u = noiseFade(x), v = noiseFade(y), w = noiseFade(z);
    int    A = s.perm[X] + Y, AA = s.perm[A] + Z, AB = s.perm[A + 1] + Z;
    int    B = s.perm[X + 1] + Y, BA = s.perm[B] + Z, BB = s.perm[B + 1] + Z;
    return lerp(lerp(lerp(noiseGrad(s.perm[AA], x, y, z), noiseGrad(s.perm[BA], x - 1, y, z), u),
                     lerp(noiseGrad(s.perm[AB], x, y - 1, z), noiseGrad(s.perm[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(noiseGrad(s.perm[AA + 1], x, y, z - 1), noiseGrad(s.perm[BA + 1], x - 1, y, z - 1), u),
                     lerp(noiseGrad(s.perm[AB + 1], x, y - 1, z - 1),
                          noiseGrad(s.perm[BB + 1], x - 1, y - 1, z - 1), u),
                     v),
                w);
}

// 叠几层（倍频）：频率翻倍、音量减半，加起来就有大轮廓也有小细节。
inline double noiseImpl(double x, double y, double z) {
    const NoiseState& s = noiseState();
    double sum = 0, amp = 1, total = 0, freq = 1;
    for (int i = 0; i < s.octaves; ++i) {
        sum += perlin1(x * freq, y * freq, z * freq) * amp;
        total += amp;
        amp *= s.falloff;
        freq *= 2;
    }
    double v = total > 0 ? sum / total : 0.0;
    return clamp(0.5 + 0.5 * v, 0.0, 1.0);   // 归一到 [0,1]，和 Processing 一样
}

}  // namespace detail

inline double noise(double x) { return detail::noiseImpl(x, 0, 0); }
inline double noise(double x, double y) { return detail::noiseImpl(x, y, 0); }
inline double noise(double x, double y, double z) { return detail::noiseImpl(x, y, z); }

// 换一串噪声（Processing 的 noiseSeed）。同一个种子永远是同一片山。
// 单独调它只改 noise()，不影响 rng()——想让画面和随机数各走各的种子就用这个；
// 全局的 seed() 会连带调它（见上面「随机数」一节），两者要一起变就用 seed()。
inline void noiseSeed(unsigned s) { detail::noiseState().reseed(s); }

// 调细节（Processing 的 noiseDetail）：层数 1~8，每层音量 falloff 0~1。
// noiseDetail(1) 是最柔和的丘陵，noiseDetail(6, 0.7) 是嶙峋的碎石。
inline void noiseDetail(int octaves, double falloff = 0.5) {
    detail::NoiseState& s = detail::noiseState();
    s.octaves = octaves < 1 ? 1 : (octaves > 8 ? 8 : octaves);
    s.falloff = clamp(falloff, 0.0, 1.0);
}

// ============================================================================
//  4. 调用栈（EASEL_CHECK 失败、崩溃捕获时打印）
// ============================================================================
namespace detail {

inline std::vector<std::string> stacktrace(int skip = 2, int maxFrames = 24) {
    std::vector<std::string> out;
#if defined(EASEL_NO_STACKTRACE)
    EASEL_UNUSED(skip); EASEL_UNUSED(maxFrames);
    out.push_back("(编译时关闭了调用栈)");
#elif defined(_WIN32)
    void*         frames[64];
    unsigned short n = ::RtlCaptureStackBackTrace((unsigned long)skip, (unsigned long)std::min(maxFrames, 64), frames, nullptr);
    for (unsigned short k = 0; k < n; ++k) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "  #%-2d 0x%p", (int)k, frames[k]);
        out.emplace_back(buf);
    }
    if (n) out.emplace_back("  （地址 -> 行号：用 CMake 的 Debug 预设构建，或在 VS Code 里 F5 运行）");
#else
    void* frames[64];
    int   n = ::backtrace(frames, std::min(maxFrames, 64));
    char** syms = ::backtrace_symbols(frames, n);
    for (int k = skip; k < n; ++k) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "  #%-2d ", k - skip);
        out.emplace_back(std::string(buf) + (syms ? syms[k] : "?"));
    }
    if (syms) std::free(syms);
#endif
    return out;
}

}  // namespace detail

// ============================================================================
//  5. 日志 / 追踪 / 断言 —— CLI 与 App 的同一行代码
// ============================================================================

enum class LogLevel { Trace = 0, Info = 1, Warn = 2, Error = 3, Check = 4 };

inline const char* levelName(LogLevel L) {
    switch (L) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Check: return "CHECK";
    }
    return "?";
}

// App 启动时会把下面这些钩子接到界面上；CLI 下它们是空的，走 printf。
namespace hooks {
using LogFn   = void (*)(LogLevel level, const char* file, int line, const char* msg);
using TraceFn = void (*)(const char* name, double value, long long index);
using CheckFn = void (*)(const char* expr, const char* msg, const char* file, int line);
using DbgFn   = void (*)(const char* key, const char* value);

inline LogFn   log   = nullptr;
inline TraceFn trace = nullptr;
inline CheckFn check = nullptr;
inline DbgFn   dbg   = nullptr;
}  // namespace hooks

// 安静模式：EASEL_TRACE 和 EASEL_LOG 不再往终端刷屏（警告和错误照打）。
// 跑测试、跑对拍的时候很有用。命令行 --quiet 也能开。
namespace detail {
inline bool g_quiet = false;
}
inline void quiet(bool on) { detail::g_quiet = on; }
inline bool quiet() { return detail::g_quiet; }

namespace detail {

// 只留文件名，不要一长串路径
inline const char* shortFile(const char* path) {
    const char* p = path;
    for (const char* c = path; *c; ++c)
        if (*c == '/' || *c == '\\') p = c + 1;
    return p;
}

inline void logImpl(LogLevel level, const char* file, int line, const char* fmt, ...) {
    char    buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (hooks::log) {
        // App：交给日志窗（它自己会同时写到真正的 stdout）
        hooks::log(level, file, line, buf);
    } else {
        if (g_quiet && level < LogLevel::Warn) return;
        std::FILE* out = (level >= LogLevel::Warn) ? stderr : stdout;
        std::fprintf(out, "[%s] %s:%d  %s\n", levelName(level), shortFile(file), line, buf);
        std::fflush(out);
    }
}

inline long long& traceCounter(const char* name) {
    // 名字不多，线性表足够；顺便保证同名 trace 的 index 连续
    static std::vector<std::pair<std::string, long long>> table;
    for (auto& kv : table)
        if (kv.first == name) return kv.second;
    table.emplace_back(name, 0);
    return table.back().second;
}

inline void traceImpl(const char* name, double value) {
    long long idx = traceCounter(name)++;
    if (hooks::trace) {
        hooks::trace(name, value, idx);
    } else if (!g_quiet) {
        // CLI：一行 CSV，可以直接 `./solver > trace.csv` 拿去画图
        std::printf("TRACE,%s,%lld,%.10g\n", name, idx, value);
    }
}

inline void checkFailed(const char* expr, const std::string& msg, const char* file, int line) {
    if (hooks::check) {
        // App：红色横幅 + 暂停回放，不崩。GUI 还活着，可以点「导出调试用例」。
        hooks::check(expr, msg.c_str(), file, line);
        return;
    }
    std::fflush(stdout);
    std::fprintf(stderr, "\n=============== EASEL_CHECK 失败 ===============\n");
    std::fprintf(stderr, "  位置：%s:%d\n", file, line);
    std::fprintf(stderr, "  条件：%s\n", expr);
    if (!msg.empty()) std::fprintf(stderr, "  说明：%s\n", msg.c_str());
    std::fprintf(stderr, "  种子：%u\n", current_seed());
    std::fprintf(stderr, "  调用栈：\n");
    for (const std::string& s : stacktrace()) std::fprintf(stderr, "%s\n", s.c_str());
    std::fprintf(stderr, "  （调用栈看不出位置？用 VS Code 的 F5 再跑一次，或者用 CMake 的 Debug 预设开 ASan）\n");
    std::fprintf(stderr, "================================================\n\n");
    std::fflush(stderr);
    // 下面的断点会触发 SIGILL；别让崩溃捕获再报一遍，那只会刷屏
    std::signal(SIGILL, SIG_DFL);
    std::signal(SIGABRT, SIG_DFL);
    EASEL_DEBUG_BREAK();
    std::abort();
}

}  // namespace detail

// 打日志。写法和 printf 一样。
//     EASEL_LOG("第 %d 轮，长度 %.2f", round, len);
#define EASEL_LOG(...)   ::easel::detail::logImpl(::easel::LogLevel::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define EASEL_WARN(...)  ::easel::detail::logImpl(::easel::LogLevel::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define EASEL_ERROR(...) ::easel::detail::logImpl(::easel::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

// 记录一个随时间变化的数：CLI 打印一行 CSV，App 自动画成折线。
//     EASEL_TRACE("路线长度", cur.length);
#define EASEL_TRACE(name, value) ::easel::detail::traceImpl((name), (double)(value))

// 断言。CLI 立刻停下并打印调用栈；App 弹红色横幅但不崩。
//     EASEL_CHECK(fabs(before - after - gain) < 1e-6, "2-opt 增量算错了");
#define EASEL_CHECK(cond, msg)                                                        \
    do {                                                                              \
        if (!(cond)) ::easel::detail::checkFailed(#cond, (msg), __FILE__, __LINE__);  \
    } while (0)

// 两个值必须相等；失败时会把两边的值都打出来。
#define EASEL_CHECK_EQ(a, b)                                                            \
    do {                                                                                \
        auto&& easel_a_ = (a);                                                          \
        auto&& easel_b_ = (b);                                                          \
        if (!(easel_a_ == easel_b_))                                                    \
            ::easel::detail::checkFailed(#a " == " #b,                                  \
                "左边 = " + ::easel::detail::stringify(easel_a_) +                      \
                "，右边 = " + ::easel::detail::stringify(easel_b_),                     \
                __FILE__, __LINE__);                                                    \
    } while (0)

// 浮点数版本：允许 eps 的误差。
#define EASEL_CHECK_NEAR(a, b, eps)                                                     \
    do {                                                                                \
        double easel_a_ = (double)(a), easel_b_ = (double)(b);                          \
        if (!(std::fabs(easel_a_ - easel_b_) <= (eps)))                                 \
            ::easel::detail::checkFailed(#a " ≈ " #b,                                   \
                "左边 = " + ::easel::detail::stringify(easel_a_) +                      \
                "，右边 = " + ::easel::detail::stringify(easel_b_) +                    \
                "，差 = " + ::easel::detail::stringify(std::fabs(easel_a_ - easel_b_)), \
                __FILE__, __LINE__);                                                    \
    } while (0)

// 在 F12 调试台的「状态」页显示一行。CLI 下什么都不做（不刷屏）。
template <class T>
inline void dbg(const char* key, const T& value) {
    if (hooks::dbg) hooks::dbg(key, detail::stringify(value).c_str());
}
inline void dbg(const char* key, const char* value) {
    if (hooks::dbg) hooks::dbg(key, value ? value : "(null)");
}

// ============================================================================
//  6. 文件（不用 <filesystem>，老编译器也能过）
// ============================================================================
namespace fs {

inline bool exists(const std::string& path) {
#if defined(_WIN32)
    return ::_access(path.c_str(), 0) == 0;
#else
    return ::access(path.c_str(), F_OK) == 0;
#endif
}

// 逐级建目录，"a/b/c" 会把 a、a/b、a/b/c 都建出来
inline bool makeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && cur != "." && cur != ".." && !exists(cur)) EASEL_MKDIR(cur.c_str());
            if (i == path.size()) break;
        }
        cur.push_back(path[i]);
    }
    return exists(path);
}

inline std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p);
}

inline std::string cwd() {
    char buf[4096];
    if (EASEL_GETCWD(buf, sizeof buf)) return buf;
    return "?";
}

inline std::string readText(const std::string& path, bool* ok = nullptr) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (ok) *ok = false; return {}; }
    std::string out;
    char        buf[8192];
    size_t      n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    if (ok) *ok = true;
    return out;
}

inline bool writeText(const std::string& path, const std::string& content) {
    std::string d = dirOf(path);
    if (!d.empty()) makeDirs(d);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return n == content.size();
}

// 读 JSON。文件不存在或格式错都返回 null，并打一条 warn（不抛异常，学生不用写 try）。
inline json loadJson(const std::string& path) {
    bool        ok = false;
    std::string text = readText(path, &ok);
    if (!ok) {
        EASEL_WARN("读不到文件：%s（当前目录 %s）", path.c_str(), cwd().c_str());
        return json();
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        EASEL_WARN("JSON 格式错误：%s", path.c_str());
        return json();
    }
    return j;
}

inline bool saveJson(const std::string& path, const json& j, int indent = 2) {
    return writeText(path, j.dump(indent) + "\n");
}

}  // namespace fs

// ============================================================================
//  7. 命令行参数
//     --seed N   --case <file>   --iters N   --open <file>   --solve
//     --dump-frame N   --doctor   --quiet
// ============================================================================
namespace cli {

struct Args {
    std::string              program = "./solver";
    std::vector<std::string> raw;
    std::vector<std::string> positional;

    bool has(const std::string& flag) const {
        for (const std::string& s : raw) {
            if (s == "--" + flag) return true;
            if (s.rfind("--" + flag + "=", 0) == 0) return true;
        }
        return false;
    }
    std::string str(const std::string& flag, const std::string& def = {}) const {
        for (size_t i = 0; i < raw.size(); ++i) {
            const std::string& s = raw[i];
            if (s == "--" + flag) return i + 1 < raw.size() ? raw[i + 1] : def;
            if (s.rfind("--" + flag + "=", 0) == 0) return s.substr(flag.size() + 3);
        }
        return def;
    }
    long long num(const std::string& flag, long long def = 0) const {
        std::string s = str(flag);
        return s.empty() ? def : std::strtoll(s.c_str(), nullptr, 10);
    }
    double real(const std::string& flag, double def = 0) const {
        std::string s = str(flag);
        return s.empty() ? def : std::strtod(s.c_str(), nullptr);
    }
    std::string at(size_t i, const std::string& def = {}) const {
        return i < positional.size() ? positional[i] : def;
    }
    std::string commandLine() const {
        std::string out = program;
        for (const std::string& s : raw) out += " " + s;
        return out;
    }
};

inline Args& args() {
    static Args a;
    return a;
}

// main 的第一行就调它。会顺手处理 --seed（D-34：不给 --seed 每次运行都不一样，
// 但用的种子永远打一行日志，想复现哪一次就把日志里的数抄进 --seed）。
inline void parse(int argc, char** argv) {
    Args& a = args();
    a.raw.clear();
    a.positional.clear();

#if defined(_WIN32)
    // argv 在 Windows 上是当前 ANSI 代码页（cp936/cp437...）的字节，不是 UTF-8——Easel 全库
    // （existsU8、widen() 等）按 UTF-8 处理字符串，直接用会把 --name 测试作品 这种参数搞乱码
    // 甚至拒收。真正的修法是用 GetCommandLineW() 重新取一份 UTF-16 的命令行，
    // CommandLineToArgvW() 切好，逐个转 UTF-8，再走下面同一套解析逻辑——但测试会拿伪造的 argv
    // （不是这个进程的真实命令行，GetCommandLineW() 返回的其实是 easel_tests.exe 自己那份）直接
    // 调 cli::parse() 来验证解析逻辑，这种情况下必须原样用传进来的 argv，不能被替换掉。
    std::vector<std::string> u8owned;    // 转换后的 UTF-8 参数，下面的指针都指向这里的存储
    std::vector<char*>       u8argv;
    int     wargc    = 0;
    LPWSTR* wargvRaw = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
    if (wargvRaw) {
        u8owned.reserve((size_t)wargc);
        for (int i = 0; i < wargc; ++i) {
            int need = ::WideCharToMultiByte(CP_UTF8, 0, wargvRaw[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s;
            if (need > 1) {
                s.resize((size_t)need - 1);   // WideCharToMultiByte 的 need 里含结尾 \0
                ::WideCharToMultiByte(CP_UTF8, 0, wargvRaw[i], -1, &s[0], need, nullptr, nullptr);
            }
            u8owned.push_back(std::move(s));
        }
        ::LocalFree(wargvRaw);

        // 判定传进来的 argv 是不是就是这个进程的真实命令行：个数得一样，且 argv[0] 和
        // wargv[0]（都转成 UTF-8、转小写、反斜杠换成正斜杠后）相等，或者其中一个是另一个的
        // 路径尾部——main() 收到的 argv[0] 可能是短名/相对路径，GetCommandLineW() 给的通常是
        // 解析过的完整路径，两者不会逐字节相等。只要判定不成立（比如测试伪造的 argv，或者
        // argc 对不上），就说明这不是真实命令行，原样使用传进来的 argv，不做任何替换。
        auto normalize = [](std::string s) {
            for (char& c : s) c = (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
            return s;
        };
        auto sameTail = [](const std::string& x, const std::string& y) {
            if (x.empty() || y.empty()) return false;
            if (x.size() >= y.size()) return x.compare(x.size() - y.size(), y.size(), y) == 0;
            return y.compare(y.size() - x.size(), x.size(), x) == 0;
        };

        bool isRealCommandLine = (argc == wargc) && argc > 0 && argv && argv[0] && !u8owned.empty() &&
                                  sameTail(normalize(argv[0]), normalize(u8owned[0]));

        if (isRealCommandLine) {
            u8argv.reserve(u8owned.size());
            for (std::string& s : u8owned) u8argv.push_back(&s[0]);
            argc = (int)u8argv.size();
            argv = u8argv.empty() ? nullptr : u8argv.data();
        }
    }
    // CommandLineToArgvW 拿不到，或者传进来的 argv 判定不是真实命令行，就照旧用传进来的 argv。
#endif

    if (argc > 0 && argv && argv[0]) a.program = argv[0];
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        a.raw.push_back(s);
        if (s.rfind("--", 0) != 0) {
            // 前一个是带值的选项就跳过，否则算位置参数
            bool consumed = false;
            if (i > 1) {
                std::string prev = argv[i - 1];
                if (prev.rfind("--", 0) == 0 && prev.find('=') == std::string::npos) {
                    // "--run" 是 workbench 自己的命令行 flag（编译并运行学生工程），和作品
                    // App 的 "--edit-run"（打开编辑栏并编译运行一次）是两个不同的东西，
                    // 但都不带值，所以都要列在这里，否则后面的位置参数会被当成它们的值吞掉。
                    static const char* valueless[] = {"--solve",    "--doctor", "--quiet", "--help",
                                                      "--debug",    "--edit",   "--edit-run", "--run"};
                    consumed = true;
                    for (const char* v : valueless)
                        if (prev == v) consumed = false;
                }
            }
            if (!consumed) a.positional.push_back(s);
        }
    }
    if (a.has("seed")) easel::seed((unsigned)a.num("seed", EASEL_FIXED_SEED));
    if (a.has("quiet")) easel::quiet(true);
    // 永远可见：不管有没有 --seed，这次用的种子都打一行日志，想复现照抄后面那串命令就行。
    EASEL_LOG("本次随机种子 %u（想复现这一次：--seed %u）", current_seed(), current_seed());
}

}  // namespace cli

// ============================================================================
//  8. 调试用例：GUI 里发现问题 -> 导出 -> 回到 CLI 复现（D-23 第 7 条）
// ============================================================================
namespace debug {

inline std::string& dir() {
    static std::string d = "debug";
    return d;
}

inline std::string caseFile(int index) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "/case-%03d.json", index);
    return dir() + buf;
}

inline int nextCaseIndex() {
    int i = 1;
    while (i < 1000 && fs::exists(caseFile(i))) ++i;
    return i;
}

// 把「现在这一刻」写成文件，并打印一行可以直接粘到终端的复现命令。
inline std::string exportCase(const json& state, const std::string& note = {}) {
    fs::makeDirs(dir());
    int         idx = nextCaseIndex();
    std::string path = caseFile(idx);

    json j;
    j["easel"] = EASEL_VERSION;
    j["note"] = note;
    j["seed"] = current_seed();
    j["command"] = cli::args().commandLine();
    std::time_t t = std::time(nullptr);
    char        stamp[64];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    j["time"] = stamp;
    j["state"] = state;

    if (!fs::saveJson(path, j)) {
        EASEL_ERROR("导出调试用例失败：写不了 %s", path.c_str());
        return {};
    }
    char cmd[1024];
    std::snprintf(cmd, sizeof cmd, "%s --case %s --seed %u", cli::args().program.c_str(), path.c_str(),
                  current_seed());
    EASEL_LOG("已导出调试用例 %s", path.c_str());
    EASEL_LOG("在终端里复现它：  %s", cmd);
    return path;
}

// 读回用例。返回的是 state 那一层；顺便把 seed 恢复成当时的值。
inline json importCase(const std::string& path) {
    json j = fs::loadJson(path);
    if (j.is_null()) return j;
    // 用例自带种子，这样才能复现出「当时那一次」。
    // 但命令行显式给了 --seed 就听命令行的（换个种子重跑，看结果稳不稳）。
    if (j.contains("seed") && !cli::args().has("seed")) {
        easel::seed(j["seed"].get<unsigned>());
    } else if (cli::args().has("seed")) {
        EASEL_LOG("命令行的 --seed %u 覆盖了用例里的种子", current_seed());
    }
    if (j.contains("note") && !j["note"].get<std::string>().empty())
        EASEL_LOG("用例说明：%s", j["note"].get<std::string>().c_str());
    return j.contains("state") ? j["state"] : j;
}

}  // namespace debug

// ============================================================================
//  9. 计时与基准
// ============================================================================
class Stopwatch {
public:
    Stopwatch() { reset(); }
    void   reset() { t0_ = std::chrono::steady_clock::now(); }
    double ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count();
    }
    double s() const { return ms() / 1000.0; }

private:
    std::chrono::steady_clock::time_point t0_;
};

namespace bench {

struct Result {
    int    repeats = 0;
    double bestMs = 0, avgMs = 0, totalMs = 0;
    std::string str() const {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%d 次：最快 %.3f ms，平均 %.3f ms，总计 %.1f ms", repeats, bestMs,
                      avgMs, totalMs);
        return buf;
    }
};

// bench::run([]{ solve(); }, 5)
template <class F>
Result run(F&& f, int repeats = 5) {
    Result r;
    r.repeats = std::max(1, repeats);
    r.bestMs = 1e300;
    for (int k = 0; k < r.repeats; ++k) {
        Stopwatch sw;
        f();
        double e = sw.ms();
        r.bestMs = std::min(r.bestMs, e);
        r.totalMs += e;
    }
    r.avgMs = r.totalMs / r.repeats;
    return r;
}

}  // namespace bench

// ============================================================================
//  10. 对拍器：用随机数据把「快的写法」和「笨但一定对的写法」跑到不一样为止
// ============================================================================
namespace duipai {

struct Report {
    bool        ok = true;
    int         rounds = 0;
    int         failedRound = -1;
    unsigned    failedSeed = 0;
    std::string casePath;
    std::string detail;
};

namespace detail_ {

template <class T, class = void>
struct HasEq : std::false_type {};
template <class T>
struct HasEq<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>>
    : std::true_type {};

template <class T>
bool same(const T& a, const T& b) {
    if constexpr (HasEq<T>::value) return a == b;
    else if constexpr (::easel::detail::HasJson<T>::value) return json(a) == json(b);
    else { static_assert(HasEq<T>::value, "对拍的返回值需要能用 == 比较，或者能转成 json"); return false; }
}

template <class T>
json asJson(const T& v) {
    if constexpr (::easel::detail::HasJson<T>::value) return json(v);
    else return json(::easel::detail::stringify(v));
}

}  // namespace detail_

// gen(Rng&) -> 输入        fa(输入) -> 结果A（要验的）        fb(输入) -> 结果B（一定对的）
// 第 k 轮用的种子是 startSeed + k，所以出错的那一轮可以单独复现。
template <class Gen, class FA, class FB>
Report run(int times, Gen gen, FA fa, FB fb, unsigned startSeed = EASEL_FIXED_SEED) {
    Report rep;
    for (int k = 0; k < times; ++k) {
        rep.rounds = k + 1;
        unsigned s = startSeed + (unsigned)k;
        Rng      r(s);
        auto     input = gen(r);
        auto     a = fa(input);
        auto     b = fb(input);
        if (detail_::same(a, b)) continue;

        rep.ok = false;
        rep.failedRound = k;
        rep.failedSeed = s;

        json j;
        j["easel"] = EASEL_VERSION;
        j["kind"] = "duipai";
        j["round"] = k;
        j["seed"] = s;
        j["input"] = detail_::asJson(input);
        j["got"] = detail_::asJson(a);
        j["want"] = detail_::asJson(b);

        fs::makeDirs(debug::dir());
        int  n = 1;
        char buf[64];
        for (;; ++n) {
            std::snprintf(buf, sizeof buf, "/duipai-%03d.json", n);
            if (!fs::exists(debug::dir() + buf) || n >= 999) break;
        }
        rep.casePath = debug::dir() + buf;
        fs::saveJson(rep.casePath, j);

        rep.detail = "第 " + std::to_string(k) + " 轮不一致（seed=" + std::to_string(s) + "）";
        std::fflush(stdout);
        std::fprintf(stderr, "\n=============== 对拍失败 ===============\n");
        std::fprintf(stderr, "  %s\n", rep.detail.c_str());
        std::fprintf(stderr, "  你的结果：%s\n", ::easel::detail::stringify(a).c_str());
        std::fprintf(stderr, "  正确结果：%s\n", ::easel::detail::stringify(b).c_str());
        std::fprintf(stderr, "  已存到：  %s\n", rep.casePath.c_str());
        std::fprintf(stderr, "  单独复现：%s --case %s --seed %u\n", cli::args().program.c_str(),
                     rep.casePath.c_str(), s);
        std::fprintf(stderr, "========================================\n\n");
        std::fflush(stderr);
        return rep;
    }
    EASEL_LOG("对拍通过：%d 轮全部一致", times);
    return rep;
}

}  // namespace duipai

// EASEL_CROSSCHECK(200, gen, myFast, bruteForce)
// 宏名是英文，但 duipai:: 这个命名空间本身是拼音，是刻意保留的例外——
// 贴近 OI/竞赛圈「对拍」这个说法，别顺手把它也改成英文。
#define EASEL_CROSSCHECK(times, gen, fa, fb) ::easel::duipai::run((times), (gen), (fa), (fb))

// ============================================================================
//  11. 崩溃捕获：段错误也要留下一句人话 + 调用栈
// ============================================================================
namespace hooks {
using CrashFn = void (*)(const char* what, const char* stack);
inline CrashFn crash = nullptr;   // App 会接管它（v0.1.1 起自动存用例）
}

namespace detail {

inline void printCrash(const char* what) {
    std::fflush(stdout);
    std::string stack;
    for (const std::string& s : stacktrace(3)) stack += s + "\n";
    std::fprintf(stderr, "\n=============== 程序崩溃了 ===============\n");
    std::fprintf(stderr, "  原因：%s\n", what);
    std::fprintf(stderr, "  种子：%u\n", current_seed());
    std::fprintf(stderr, "  命令：%s\n", cli::args().commandLine().c_str());
    std::fprintf(stderr, "  调用栈：\n%s", stack.c_str());
    std::fprintf(stderr, "  （信号处理里的调用栈常常只有半截。想看准确位置：VS Code F5，或 Debug 预设开 ASan）\n");
    std::fprintf(stderr, "==========================================\n\n");
    std::fflush(stderr);
    if (hooks::crash) hooks::crash(what, stack.c_str());
}

inline void signalHandler(int sig) {
    const char* what = "未知信号";
    switch (sig) {
        case SIGSEGV: what = "SIGSEGV 段错误（多半是数组越界，或者用了空指针 / 失效的迭代器）"; break;
        case SIGABRT: what = "SIGABRT 主动终止（assert 失败、未捕获的异常、或者标准库检查报错）"; break;
        case SIGFPE:  what = "SIGFPE 算术错误（整数除以 0）"; break;
        case SIGILL:  what = "SIGILL 非法指令（EASEL_CHECK 失败时的断点也会走这里）"; break;
        default: break;
    }
    printCrash(what);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

inline void terminateHandler() {
    printCrash("未捕获的异常（try/catch 之外抛了东西）");
    std::abort();
}

}  // namespace detail

// 建议在 main 的开头调一次。
inline void installCrashHandler() {
    std::signal(SIGSEGV, detail::signalHandler);
    std::signal(SIGABRT, detail::signalHandler);
    std::signal(SIGFPE, detail::signalHandler);
    std::signal(SIGILL, detail::signalHandler);
    std::set_terminate(detail::terminateHandler);
}

// ============================================================================
//  12. --doctor：出问题时第一件事
// ============================================================================
inline std::string doctorCore() {
    std::ostringstream o;
    o << "Easel " << EASEL_VERSION << "  自检（core）\n";
    o << "  编译器      : ";
#if defined(__clang__)
    o << "clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#elif defined(_MSC_VER)
    o << "MSVC " << _MSC_VER;
#elif defined(__GNUC__)
    o << "gcc " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
    o << "未知";
#endif
    o << "\n";
    o << "  C++ 标准    : " <<
#if defined(_MSVC_LANG)
        _MSVC_LANG
#else
        __cplusplus
#endif
      << "\n";
    o << "  平台        : ";
#if defined(_WIN32)
    o << "Windows";
#elif defined(__APPLE__)
    o << "macOS";
#else
    o << "Linux/其它";
#endif
    o << "  " << (int)(sizeof(void*) * 8) << " 位\n";
    o << "  构建类型    : ";
#ifdef NDEBUG
    o << "Release（NDEBUG 已定义，断言检查被关掉了）";
#else
    o << "Debug";
#endif
    o << "\n";
    o << "  越界检查    : ";
#if defined(__SANITIZE_ADDRESS__)
    o << "ASan 开";
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    o << "ASan 开";
#  else
    o << "ASan 关";
#  endif
#else
    o << "ASan 关";
#endif
#if defined(_GLIBCXX_ASSERTIONS)
    o << " · libstdc++ 断言 开";
#elif defined(_LIBCPP_HARDENING_MODE)
    o << " · libc++ hardening 开";
#elif defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
    o << " · MSVC 迭代器检查 开";
#else
    o << " · 标准库检查 关（用 CMake 的 Debug 预设可以打开）";
#endif
    o << "\n";
    o << "  调用栈      : ";
#if defined(EASEL_NO_STACKTRACE)
    o << "关";
#else
    o << "开";
#endif
    o << "\n";
    o << "  随机种子    : " << current_seed() << "（本次运行随机生成；--seed N 可以指定复现哪一次）\n";
    o << "  工作目录    : " << fs::cwd() << "\n";
    o << "  程序        : " << cli::args().program << "\n";
    o << "  调试用例目录: " << debug::dir() << (fs::exists(debug::dir()) ? "（存在）" : "（还没有，第一次导出时自动建）") << "\n";
    return o.str();
}

}  // namespace easel

#endif  // EASEL_CORE_H
