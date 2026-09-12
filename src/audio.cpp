// Easel — audio.cpp  声音（miniaudio 的高层 engine API，D-33 批次 2）
//
// 这个文件里有整个库唯一的一处线程：miniaudio 的数据回调跑在音频线程上。
// 它只往一个固定大小的环形缓冲区里写最近的采样（不加锁、不分配、不调用任何
// 别的东西），主线程只读。loudness() / spectrum() 就是在主线程里读这一份拷贝。
// 头文件里一个线程相关的字都没有 —— 学生不该碰线程（D-11）。
#include "internal.h"

// miniaudio 是单头库。这里先只要它的声明，九万行的实现放在本文件最末尾编一次。
// 它自己的告警不归我们管，用 pragma 圈起来；圈外我们自己的代码照样 -Wall -Wextra。
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <miniaudio.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <atomic>
#include <cmath>

namespace easel {

namespace {

// π 现在是 core.h 里 easel::kPi 公开常量，这里不用再自己定义一份（定义了反而会跟
// easel::kPi 撞名、产生二义性——它是通过匿名命名空间隐含的 using-directive 注入到
// easel 这一层的，和直接写在 easel:: 下的 kPi 处在同一个查找层级）。

// ============================================================================
//  环形缓冲区 —— 音频线程写，主线程读
//  只有一个写者、一个读者，写指针用 atomic，样本本身也是 atomic<float>
//  （relaxed 就够：读到半新半旧的一帧最多让柱子抖一下，但不能是未定义行为）。
//  读者慢一拍时会被追尾，那就少看一眼历史 —— 声音本来就是「此刻」的事。
// ============================================================================
constexpr int kRingN     = 4096;   // 2 的幂，取模用位与
constexpr int kAnalysisN = 2048;   // 分析窗口：48 kHz 下约 43 ms

struct Ring {
    std::atomic<float>              data[kRingN];
    std::atomic<unsigned long long> w{0};
};
Ring g_ring;

// 音频线程唯一做的事：把混好的这一段抄进环里（多声道先平均成单声道）
void ringWrite(const float* frames, unsigned long long count, unsigned channels) {
    if (!frames || channels == 0) return;
    unsigned long long w = g_ring.w.load(std::memory_order_relaxed);
    for (unsigned long long i = 0; i < count; ++i) {
        float s = 0.f;
        for (unsigned c = 0; c < channels; ++c) s += frames[i * channels + c];
        g_ring.data[(size_t)(w & (kRingN - 1))].store(s / (float)channels,
                                                      std::memory_order_relaxed);
        ++w;
    }
    g_ring.w.store(w, std::memory_order_release);
}

// 主线程：取最近 n 个采样（按时间顺序）。返回真的取到几个（刚开声时可能不足）。
int ringRead(float* out, int n) {
    if (!out || n <= 0) return 0;
    if (n > kRingN) n = kRingN;
    unsigned long long w = g_ring.w.load(std::memory_order_acquire);
    int got = (int)((w < (unsigned long long)n) ? w : (unsigned long long)n);
    for (int i = 0; i < got; ++i) {
        unsigned long long idx = w - (unsigned long long)got + (unsigned long long)i;
        out[i] = g_ring.data[(size_t)(idx & (kRingN - 1))].load(std::memory_order_relaxed);
    }
    return got;
}

void ringClear() {
    for (int i = 0; i < kRingN; ++i) g_ring.data[i].store(0.f, std::memory_order_relaxed);
    g_ring.w.store(0, std::memory_order_release);
}

// ============================================================================
//  设备与引擎 —— 懒初始化，失败就一直静默
// ============================================================================

// Engine 这个单例对象本身是否还活着（不是"设备开着没开着"，是"这个 C++ 对象本身还
// 在不在"）。正常运行期间恒为 true；只有在程序真正退出、下面那个函数级 static 被销毁
// 之后才变 false。全局的 audio::Sound（析构要等 main 之后的静态析构阶段才轮到，见
// audioState() 的注释）、以及再往下那个兜底的 Closer，都可能在这之后还想碰 Engine——
// 用这个标志挡住，别 use-after-destroy。普通 bool，没有自己的析构函数，天然活到最后。
bool g_engineAlive = false;

// 学生 load 出来的 Sound 的实体。放在这里是为了让 stop()「全停」能够到它们。
struct SoundNode {
    ma_sound snd;
    bool     ok = false;
    ~SoundNode() {
        // g_engineAlive 兜底：正常情况下 closeEngine() 早就把 ok 置回 false 了（无论
        // 是 App::~App 显式调用，还是这次真的没人管——Engine 自己销毁前也没机会通知
        // 我们），这里多一层是防"Engine 已经死了但 ok 还没来得及被清"这种极端时序，
        // 避免对着一个已经不存在的 ma_engine 调 ma_sound_uninit。
        if (ok && g_engineAlive) ma_sound_uninit(&snd);
    }
};

struct Engine {
    bool        tried = false;    // 试过开设备了没（失败也只试一次）
    bool        ok = false;
    ma_engine   engine;
    double      vol = 1.0;
    unsigned    channels = 2;
    unsigned    sampleRate = 48000;
    std::string deviceName, backendName, err;

    std::vector<ma_sound*>              oneshots;   // play() / loop() 起的
    std::vector<std::weak_ptr<SoundNode>> sounds;   // 学生手里的 Sound

    Engine() { g_engineAlive = true; }
    ~Engine() { g_engineAlive = false; }
};

// 单例，函数内 static（Meyers 单例，写法照抄 core.h 里的 rng()）。以前 g_audio 是
// 命名空间级的全局对象，含 std::string/std::vector——学生写
// `struct State { audio::Sound s = audio::load(...); } state;` 这种全局，state 在别的翻译
// 单元里，跟这个翻译单元里的 g_audio 谁先构造是未定义行为：赌输了就是 state 的构造函数
// 里摸到一个还没构造出来的 g_audio，崩在 main 之前，还没有调用栈——最难查的一类崩溃。
// 改成函数内 static 后，Engine 只会在第一次真的被用到（第一次调 ensure()/audioState()）
// 时才构造，不管这次调用是从哪个翻译单元、哪个全局的构造函数里发起的，都不会有「哪个
// 全局先构造」的问题。
Engine& audioState() {
    static Engine s;
    return s;
}

// 音频线程：ma_engine 每混完一段就叫我们一次（ma_engine_config::onProcess）
void onProcess(void* user, float* frames, ma_uint64 count) {
    (void)user;
    ringWrite(frames, count, audioState().channels);
}

void closeEngine();

bool ensure() {
    Engine& g_audio = audioState();
    if (g_audio.tried) return g_audio.ok;
    g_audio.tried = true;

    ma_engine_config cfg = ma_engine_config_init();
    cfg.onProcess = onProcess;
    // 先别自动开：拿到声道数之后再 start，回调不会跑在我们准备好之前
    cfg.noAutoStart = MA_TRUE;

    ma_result r = ma_engine_init(&cfg, &g_audio.engine);
    if (r != MA_SUCCESS) {
        g_audio.err = ma_result_description(r);
        // 机房电脑可能根本没声卡。警告一次就够，之后所有 audio:: 调用静默返回。
        EASEL_WARN("打不开音频设备（%s）。声音全部跳过，程序照常跑。", g_audio.err.c_str());
        return false;
    }
    g_audio.channels = ma_engine_get_channels(&g_audio.engine);
    g_audio.sampleRate = ma_engine_get_sample_rate(&g_audio.engine);
    if (g_audio.channels == 0) g_audio.channels = 2;
    ma_device* dev = ma_engine_get_device(&g_audio.engine);
    if (dev) {
        g_audio.deviceName = dev->playback.name;
        if (dev->pContext) g_audio.backendName = ma_get_backend_name(dev->pContext->backend);
    }
    ringClear();

    r = ma_engine_start(&g_audio.engine);
    if (r != MA_SUCCESS) {
        g_audio.err = ma_result_description(r);
        ma_engine_uninit(&g_audio.engine);
        EASEL_WARN("音频设备开起来了却启动不了（%s）。声音全部跳过。", g_audio.err.c_str());
        return false;
    }
    ma_engine_set_volume(&g_audio.engine, (float)g_audio.vol);
    g_audio.ok = true;
    EASEL_LOG("声音：%s · %s · %u Hz · %u 声道", g_audio.backendName.c_str(),
              g_audio.deviceName.c_str(), g_audio.sampleRate, g_audio.channels);
    return true;
}

// 把放完了的一次性声音收掉（循环的永远不会「放完」，得等 stop()）
void collect() {
    Engine& g_audio = audioState();
    for (size_t i = 0; i < g_audio.oneshots.size();) {
        ma_sound* s = g_audio.oneshots[i];
        if (ma_sound_at_end(s)) {
            ma_sound_uninit(s);
            delete s;
            g_audio.oneshots.erase(g_audio.oneshots.begin() + (long)i);
        } else {
            ++i;
        }
    }
    // 顺手把学生已经销毁的 Sound 从名单里划掉
    for (size_t i = 0; i < g_audio.sounds.size();) {
        if (g_audio.sounds[i].expired())
            g_audio.sounds.erase(g_audio.sounds.begin() + (long)i);
        else
            ++i;
    }
}

void stopAll() {
    Engine& g_audio = audioState();
    for (ma_sound* s : g_audio.oneshots) {
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    g_audio.oneshots.clear();
    for (std::weak_ptr<SoundNode>& w : g_audio.sounds) {
        std::shared_ptr<SoundNode> n = w.lock();
        if (n && n->ok) ma_sound_stop(&n->snd);
    }
}

// 关设备。学生手里可能还攥着 Sound —— 先替它们把 ma_sound 拆了并标记成空，
// 免得引擎没了之后它们的析构函数再去动一块已经不存在的内存。
void closeEngine() {
    // g_engineAlive 为 false 只有两种可能：Engine 从来没被真正用过（这次直接跳过，
    // 没什么要关的），或者 Engine 的静态局部对象已经在程序退出时被销毁了（这时候
    // audioState() 会返回一个指向已销毁对象的引用，碰它的成员是 UB）——两种情况下
    // 「什么都不做」都是安全且正确的，所以在摸 audioState() 之前先挡在这里。
    if (!g_engineAlive) return;
    Engine& g_audio = audioState();
    if (!g_audio.ok) {
        g_audio.tried = false;
        return;
    }
    stopAll();
    for (std::weak_ptr<SoundNode>& w : g_audio.sounds) {
        std::shared_ptr<SoundNode> n = w.lock();
        if (n && n->ok) {
            ma_sound_uninit(&n->snd);
            n->ok = false;
        }
    }
    g_audio.sounds.clear();
    ma_engine_uninit(&g_audio.engine);
    g_audio.ok = false;
    g_audio.tried = false;   // 之后再调 audio::，重新开一次设备
    ringClear();
}

// 兜底：万一谁忘了在退出时调 internal::audioShutdown()，进程结束前也尽量关干净。
// 以前这里靠"声明在 g_audio 之后，所以 Closer 先被销毁、engine 还在"来保证顺序；
// 现在 g_audio 是 Meyers 单例（audioState() 里的函数内 static），构造时机跟着
// "第一次真的用到声音"走，跟这个命名空间级的 Closer 的构造时机对不上——Closer 几乎
// 总是先构造（TU 的静态初始化阶段，早于任何一次真正的音频调用），那么反过来，销毁
// 时 Engine 会先死、Closer 后死，到 Closer 的析构函数这里 Engine 已经不在了。
// 所以 closeEngine() 自己用 g_engineAlive 挡一下：Engine 已经不在了就什么都不做，
// 不会 use-after-destroy——不再依赖这两个对象谁先声明、谁先构造。
struct Closer {
    ~Closer() { closeEngine(); }
};
Closer g_closer;

// 打开一个文件。Windows 上中文路径要走宽字符那套（和 internal::fopenU8 一个道理）。
ma_result initSoundFromFile(const std::string& path, ma_uint32 flags, ma_sound* out) {
    Engine& g_audio = audioState();
#if defined(_WIN32)
    return ma_sound_init_from_file_w(&g_audio.engine, internal::widen(path).c_str(), flags, nullptr,
                                     nullptr, out);
#else
    return ma_sound_init_from_file(&g_audio.engine, path.c_str(), flags, nullptr, nullptr, out);
#endif
}

bool startFile(const std::string& path, bool looping) {
    if (!ensure()) return false;
    Engine& g_audio = audioState();
    collect();
    // 一次性音效整段解码（放的时候不卡）；背景音乐边放边解（几分钟的 mp3 也秒开）
    ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION |
                      (looping ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);
    ma_sound*  s = new ma_sound();
    ma_result  r = initSoundFromFile(path, flags, s);
    if (r != MA_SUCCESS) {
        delete s;
        EASEL_WARN("放不了 %s（%s）", path.c_str(), ma_result_description(r));
        return false;
    }
    ma_sound_set_looping(s, looping ? MA_TRUE : MA_FALSE);
    if (ma_sound_start(s) != MA_SUCCESS) {
        ma_sound_uninit(s);
        delete s;
        return false;
    }
    g_audio.oneshots.push_back(s);
    return true;
}

}  // namespace

// ============================================================================
//  库内部：自检、退出释放，以及给测试用的纯函数（都不碰声卡）
// ============================================================================
namespace internal {

void audioShutdown() { closeEngine(); }

std::string audioDoctor() {
    ensure();   // 自检就是要真的去开一次，不然什么也没说
    Engine& g_audio = audioState();
    if (g_audio.ok) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s · %s · %u Hz · %u 声道", g_audio.backendName.c_str(),
                      g_audio.deviceName.c_str(), g_audio.sampleRate, g_audio.channels);
        return buf;
    }
    return "打不开（" + (g_audio.err.empty() ? std::string("没有可用的播放设备") : g_audio.err) +
           "）—— 声音全部跳过，别的功能不受影响";
}

// 原地 radix-2 FFT。n 必须是 2 的幂，不是就原样返回。
void audioFFT(float* re, float* im, int n) {
    if (!re || !im || n <= 1 || (n & (n - 1)) != 0) return;
    // 位反转重排
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / (double)len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;    // 旋转因子用 double 累乘，2048 点也不会飘
            for (int k = 0; k < len / 2; ++k) {
                const int   a = i + k, b = i + k + len / 2;
                const double vr = re[b] * cr - im[b] * ci;
                const double vi = re[b] * ci + im[b] * cr;
                re[b] = (float)(re[a] - vr);
                im[b] = (float)(im[a] - vi);
                re[a] = (float)(re[a] + vr);
                im[a] = (float)(im[a] + vi);
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

// 把 1..fftN/2 这些 bin 按对数分成 bands 段（左闭右开写进 lo/hi）。
// 为什么是对数：人耳听音高是对数的，线性分段的话低音全挤在第一根柱子里。
void audioBandBins(int bands, int fftN, double sampleRate, int* lo, int* hi) {
    if (bands <= 0 || fftN < 4 || sampleRate <= 0 || !lo || !hi) return;
    const double fLo = 40.0;                                    // 再低就只剩直流和噪声
    const double fHi = std::min(sampleRate * 0.5, 16000.0);     // 再高学生也听不见
    const double binHz = sampleRate / (double)fftN;
    const int    maxBin = fftN / 2;
    for (int b = 0; b < bands; ++b) {
        double f0 = fLo * std::pow(fHi / fLo, (double)b / bands);
        double f1 = fLo * std::pow(fHi / fLo, (double)(b + 1) / bands);
        int    a = (int)std::floor(f0 / binHz);
        int    c = (int)std::ceil(f1 / binHz);
        if (a < 1) a = 1;
        if (a > maxBin - 1) a = maxBin - 1;
        if (c > maxBin) c = maxBin;
        if (c <= a) c = a + 1;    // 最低那几段会落进同一个 bin，至少给一个
        lo[b] = a;
        hi[b] = c;
    }
}

// 一段单声道采样 → bands 个 0..1 的柱子。纯函数，没有平滑，测试直接调。
// n 必须是 2 的幂（内部就用它当 FFT 长度）。
void audioAnalyze(const float* mono, int n, int bands, double sampleRate, float* out) {
    if (!out || bands <= 0) return;
    for (int i = 0; i < bands; ++i) out[i] = 0.f;
    if (!mono || n < 4 || (n & (n - 1)) != 0 || sampleRate <= 0) return;

    std::vector<float> re((size_t)n), im((size_t)n, 0.f);
    for (int i = 0; i < n; ++i) {
        // Hann 窗：不加窗的话一个不整周期的正弦会糊成一片（频谱泄漏）
        const float w = 0.5f * (1.f - (float)std::cos(2.0 * kPi * i / (n - 1)));
        re[(size_t)i] = mono[i] * w;
    }
    audioFFT(re.data(), im.data(), n);

    std::vector<int> lo((size_t)bands), hi((size_t)bands);
    audioBandBins(bands, n, sampleRate, lo.data(), hi.data());
    // 4/n：单边谱要乘 2，Hann 的相干增益是 0.5 再乘 2 —— 满幅正弦的峰值正好读成 1.0
    const float scale = 4.f / (float)n;
    for (int b = 0; b < bands; ++b) {
        float peak = 0.f;
        for (int k = lo[(size_t)b]; k < hi[(size_t)b] && k <= n / 2; ++k) {
            const float m =
                std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]) * scale;
            if (m > peak) peak = m;
        }
        out[b] = peak > 1.f ? 1.f : peak;
    }
}

// 测试用：假装自己是音频线程往环里写 / 像 loudness() 那样读最近的采样
void audioFeedForTest(const float* frames, int count, int channels) {
    ringWrite(frames, (unsigned long long)(count < 0 ? 0 : count), (unsigned)(channels < 1 ? 1 : channels));
}
int audioReadRecent(float* out, int n) { return ringRead(out, n); }
void audioClearRing() { ringClear(); }

}  // namespace internal

// ============================================================================
//  学生看到的 API
// ============================================================================
namespace audio {

bool play(const std::string& path) { return startFile(path, false); }
bool loop(const std::string& path) { return startFile(path, true); }

void stop() {
    Engine& g_audio = audioState();
    if (!g_audio.ok) return;
    stopAll();
}

void volume(double v) {
    Engine& g_audio = audioState();
    g_audio.vol = clamp(v, 0.0, 1.0);
    if (ensure()) ma_engine_set_volume(&g_audio.engine, (float)g_audio.vol);
}
double volume() { return audioState().vol; }

double loudness() {
    ensure();   // 没设备也无所谓：环里一直是空的，读出来就是 0
    static double smoothed = 0.0;
    float         buf[kAnalysisN];
    const int     got = ringRead(buf, kAnalysisN);
    double        sum = 0.0;
    for (int i = 0; i < got; ++i) sum += (double)buf[i] * buf[i];
    // ×√2：满幅正弦的 RMS 是 0.707，这样读出来正好是 1.0
    double v = got > 0 ? std::sqrt(sum / got) * 1.41421356 : 0.0;
    v = clamp(v, 0.0, 1.0);
    // 快起慢落：声音一来立刻顶上去，停了慢慢掉 —— 和 Scratch 的响度、和真表针一样
    smoothed = v > smoothed ? v : smoothed * 0.88 + v * 0.12;
    return smoothed;
}

std::vector<float> spectrum(int bands) {
    if (bands < 1) bands = 1;
    if (bands > 512) bands = 512;
    static std::vector<float> smoothed;
    if ((int)smoothed.size() != bands) smoothed.assign((size_t)bands, 0.f);

    ensure();
    float     buf[kAnalysisN];
    const int got = ringRead(buf, kAnalysisN);
    std::vector<float> now((size_t)bands, 0.f);
    if (got == kAnalysisN) {
        internal::audioAnalyze(buf, kAnalysisN, bands,
                               (double)(audioState().sampleRate ? audioState().sampleRate : 48000),
                               now.data());
    }
    // 时间平滑：柱子涨得快、落得慢（0.8 的衰减），不然每帧一个样，看着像噪点
    for (int i = 0; i < bands; ++i) {
        const float a = now[(size_t)i], b = smoothed[(size_t)i] * 0.8f;
        smoothed[(size_t)i] = a > b ? a : b;
    }
    return smoothed;
}

bool        ok() { return ensure(); }
std::string doctor() { return internal::audioDoctor(); }

// ---------------------------------------------------------------- Sound
struct Sound::Impl : SoundNode {};

Sound Sound::load(const std::string& path) {
    Sound s;
    if (!ensure()) return s;
    collect();
    std::shared_ptr<Impl> impl = std::make_shared<Impl>();
    ma_result             r = initSoundFromFile(
        path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, &impl->snd);
    if (r != MA_SUCCESS) {
        EASEL_WARN("加载不了 %s（%s）", path.c_str(), ma_result_description(r));
        return s;   // 空句柄：if (!s) 判得出来，照样能安全地调 play()
    }
    impl->ok = true;
    s.p_ = impl;
    audioState().sounds.push_back(std::weak_ptr<SoundNode>(impl));
    return s;
}

void Sound::play() {
    if (!p_ || !p_->ok) return;
    ma_sound_set_looping(&p_->snd, loop ? MA_TRUE : MA_FALSE);
    ma_sound_seek_to_pcm_frame(&p_->snd, 0);   // 还在响就从头重放（和 Scratch 一样）
    ma_sound_start(&p_->snd);
}
void Sound::stop() {
    if (!p_ || !p_->ok) return;
    ma_sound_stop(&p_->snd);
}
void Sound::volume(double v) {
    if (!p_ || !p_->ok) return;
    ma_sound_set_volume(&p_->snd, (float)clamp(v, 0.0, 1.0));
}
void Sound::pitch(double p) {
    if (!p_ || !p_->ok) return;
    ma_sound_set_pitch(&p_->snd, (float)clamp(p, 0.05, 8.0));
}

}  // namespace audio
}  // namespace easel

// ============================================================================
//  miniaudio 的实现 —— 九万行，整个工程里只在这里编一次（十几秒，之后有 .o 缓存）。
//  为什么放在文件最末尾：它的实现部分里 #pragma diagnostic push 比 pop 多两个，
//  写在文件开头的话，我们「关掉它的告警」那一圈会被它带偏，弹不回来 ——
//  下面我们自己的代码就再也收不到 -Wall -Wextra 了。放最后，谁也影响不到。
//  （所以这里只 push 不 pop，后面已经没有代码了。）
// ============================================================================
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
