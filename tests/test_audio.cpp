// 声音的测试（D-33 批次 2）。
// 这里一个断言都不依赖真实声卡：FFT、分频段、环形缓冲区都是纯函数，
// 剩下的检查的是「没有声音设备时也不崩、返回值守约定」—— CI 上无头也能跑。
#include <doctest/doctest.h>

#include "../src/internal.h"

#include <cmath>
#include <vector>

using namespace easel;
using namespace easel::internal;

namespace {
// kPi 现在是 core.h 里的 easel::kPi（`using namespace easel` 已经带进来了），
// 这里不用再定义一份同名的——定义了反而会和它撞名产生二义性。

// 一段正弦波，幅度 1.0（满幅）
std::vector<float> sine(int n, double freq, double sampleRate, float amp = 1.f) {
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = amp * (float)std::sin(2.0 * kPi * freq * i / sampleRate);
    return v;
}
}  // namespace

TEST_CASE("FFT：正弦波的峰值落在正确的 bin 上") {
    const int n = 1024;
    // 整周期：第 64 个 bin 正好是 64 个周期，不加窗也不会泄漏
    std::vector<float> re(n), im((size_t)n, 0.f);
    for (int i = 0; i < n; ++i) re[(size_t)i] = (float)std::sin(2.0 * kPi * 64 * i / n);
    audioFFT(re.data(), im.data(), n);

    int   peak = -1;
    float best = -1.f;
    for (int k = 1; k <= n / 2; ++k) {
        float m = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
        if (m > best) { best = m; peak = k; }
    }
    CHECK(peak == 64);
    // 满幅正弦：单边幅值 = 2*|X|/n 应该是 1
    CHECK(2.0 * best / n == doctest::Approx(1.0).epsilon(0.01));
    // 别的 bin 基本是零
    float other = std::sqrt(re[100] * re[100] + im[100] * im[100]);
    CHECK(other < best * 0.01f);
}

TEST_CASE("FFT：常数信号的能量全在 bin 0；参数不合法时原样返回") {
    const int          n = 64;
    std::vector<float> re((size_t)n, 1.f), im((size_t)n, 0.f);
    audioFFT(re.data(), im.data(), n);
    CHECK(re[0] == doctest::Approx((float)n));
    CHECK(std::fabs(re[1]) < 1e-3f);

    // 不是 2 的幂 —— 什么都不做，不越界不崩
    std::vector<float> r2{1, 2, 3}, i2{0, 0, 0};
    audioFFT(r2.data(), i2.data(), 3);
    CHECK(r2[0] == doctest::Approx(1.0));
    audioFFT(nullptr, nullptr, 1024);   // 空指针也不能崩
}

TEST_CASE("频段划分：对数分段、递增、不越界") {
    const int bands = 64, fftN = 2048;
    const double sr = 48000;
    std::vector<int> lo((size_t)bands), hi((size_t)bands);
    audioBandBins(bands, fftN, sr, lo.data(), hi.data());

    for (int b = 0; b < bands; ++b) {
        CHECK(lo[(size_t)b] >= 1);              // 直流不要
        CHECK(hi[(size_t)b] > lo[(size_t)b]);   // 每段至少一个 bin
        CHECK(hi[(size_t)b] <= fftN / 2);
        if (b > 0) CHECK(lo[(size_t)b] >= lo[(size_t)b - 1]);
    }
    // 对数：低频那几段窄，高频那几段宽
    CHECK(hi[63] - lo[63] > hi[0] - lo[0]);
    // 第一段从 40 Hz 附近开始，最后一段到 16 kHz 附近为止
    const double binHz = sr / fftN;
    CHECK(lo[0] * binHz < 60.0);
    CHECK(hi[63] * binHz > 12000.0);

    audioBandBins(0, fftN, sr, lo.data(), hi.data());   // 参数不合法：不崩
    audioBandBins(bands, fftN, sr, nullptr, nullptr);
}

TEST_CASE("频谱：440 Hz 的正弦落在含 440 Hz 的那一段，且幅值接近 1") {
    const int    n = 2048, bands = 64;
    const double sr = 48000, freq = 440.0;
    std::vector<float> wave = sine(n, freq, sr);
    std::vector<float> out((size_t)bands, 0.f);
    audioAnalyze(wave.data(), n, bands, sr, out.data());

    int   peak = 0;
    float best = -1.f;
    for (int b = 0; b < bands; ++b)
        if (out[(size_t)b] > best) { best = out[(size_t)b]; peak = b; }

    std::vector<int> lo((size_t)bands), hi((size_t)bands);
    audioBandBins(bands, n, sr, lo.data(), hi.data());
    const double binHz = sr / n;
    const double f0 = lo[(size_t)peak] * binHz, f1 = hi[(size_t)peak] * binHz;
    INFO("峰值段 ", peak, " = ", f0, "..", f1, " Hz，值 ", best);
    // 窗有宽度，440 落在这一段里或者紧挨着的一段
    CHECK(f0 - binHz * 2 <= freq);
    CHECK(f1 + binHz * 2 >= freq);
    // 满幅正弦，加了 Hann 窗之后归一化过 —— 应该接近 1
    CHECK(best > 0.7f);
    CHECK(best <= 1.0f);
    // 高频段应该是安静的
    CHECK(out[63] < 0.05f);

    // 全 0 的输入 → 全 0 的柱子；长度不是 2 的幂 → 全 0，不崩
    std::vector<float> quiet((size_t)n, 0.f);
    audioAnalyze(quiet.data(), n, bands, sr, out.data());
    for (int b = 0; b < bands; ++b) CHECK(out[(size_t)b] == doctest::Approx(0.0));
    audioAnalyze(quiet.data(), 1000, bands, sr, out.data());
    CHECK(out[0] == doctest::Approx(0.0));
    audioAnalyze(quiet.data(), n, bands, sr, nullptr);
}

TEST_CASE("环形缓冲区：写进去的能按时间顺序读回来") {
    // 先把设备关掉：不然音频线程也在往同一个环里写静音，读回来的就不是我们写的了
    audioShutdown();
    audioClearRing();
    float buf[64] = {0};
    CHECK(audioReadRecent(buf, 8) == 0);   // 一个采样都还没有

    // 单声道：直接写
    std::vector<float> a{1, 2, 3, 4, 5};
    audioFeedForTest(a.data(), 5, 1);
    CHECK(audioReadRecent(buf, 8) == 5);   // 只有 5 个，就给 5 个
    CHECK(buf[0] == doctest::Approx(1.0));
    CHECK(buf[4] == doctest::Approx(5.0));

    // 只要最近的 3 个
    CHECK(audioReadRecent(buf, 3) == 3);
    CHECK(buf[0] == doctest::Approx(3.0));
    CHECK(buf[2] == doctest::Approx(5.0));

    // 立体声：两个声道平均成一个
    audioClearRing();
    std::vector<float> st{0.f, 1.f, 2.f, 4.f};   // 两帧：(0,1) (2,4)
    audioFeedForTest(st.data(), 2, 2);
    CHECK(audioReadRecent(buf, 2) == 2);
    CHECK(buf[0] == doctest::Approx(0.5));
    CHECK(buf[1] == doctest::Approx(3.0));

    audioReadRecent(nullptr, 4);      // 参数不合法：不崩
    audioReadRecent(buf, 0);
    audioFeedForTest(nullptr, 4, 2);
    audioFeedForTest(a.data(), 5, 0);
}

TEST_CASE("环形缓冲区：写满一圈之后只留最近的那些") {
    audioShutdown();
    audioClearRing();
    // 写 10000 个（环是 4096 个），最近的应该是 9999
    std::vector<float> big(10000);
    for (int i = 0; i < 10000; ++i) big[(size_t)i] = (float)i;
    audioFeedForTest(big.data(), 10000, 1);

    std::vector<float> out(4096);
    const int          got = audioReadRecent(out.data(), 8192);   // 要得比环大
    CHECK(got == 4096);                                           // 最多给一环
    CHECK(out[4095] == doctest::Approx(9999.0));
    CHECK(out[0] == doctest::Approx(10000.0 - 4096));
    for (int i = 1; i < got; ++i) CHECK(out[(size_t)i] > out[(size_t)i - 1]);
    audioClearRing();
}

TEST_CASE("Sound 空句柄：怎么用都不崩") {
    audio::Sound s;                       // 没 load 过
    CHECK_FALSE((bool)s);
    s.play();                             // 全是安全的空操作
    s.stop();
    s.volume(0.5);
    s.pitch(2.0);
    s.loop = true;
    s.play();
    audio::Sound copy = s;                // 可拷贝（内部是共享指针）
    CHECK_FALSE((bool)copy);

    // 文件不存在 —— 不管有没有声卡，返回的都是空句柄，而不是崩溃
    audio::Sound bad = audio::load("这个文件根本不存在.wav");
    CHECK_FALSE((bool)bad);
    bad.play();
    bad.volume(0.1);
}

TEST_CASE("没有声音设备时：各函数返回约定值，程序不崩") {
    // 这台机器可能有声卡也可能没有，两种情况下面这些都必须成立。
    CHECK_FALSE(audio::play("这个文件根本不存在.wav"));
    CHECK_FALSE(audio::loop("这个文件根本不存在.mp3"));
    audio::stop();          // 没在放也能停
    audio::stop();

    audio::volume(0.25);
    CHECK(audio::volume() == doctest::Approx(0.25));
    audio::volume(5.0);     // 超范围夹回去
    CHECK(audio::volume() == doctest::Approx(1.0));
    audio::volume(-1.0);
    CHECK(audio::volume() == doctest::Approx(0.0));
    audio::volume(1.0);

    const double lv = audio::loudness();
    CHECK(lv >= 0.0);
    CHECK(lv <= 1.0);

    std::vector<float> f = audio::spectrum();
    CHECK(f.size() == 64);
    f = audio::spectrum(16);
    CHECK(f.size() == 16);
    for (float v : f) {
        CHECK(v >= 0.f);
        CHECK(v <= 1.f);
    }
    CHECK(audio::spectrum(0).size() == 1);       // 荒唐的参数也得给个说得过去的结果
    CHECK(audio::spectrum(100000).size() == 512);

    CHECK_FALSE(audio::doctor().empty());        // 有声卡说设备名，没声卡说原因
    (void)audio::ok();                           // 有没有设备都不该崩
    internal::audioShutdown();                   // 关两次也没事
    internal::audioShutdown();
}
