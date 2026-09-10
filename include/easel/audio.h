// Easel — audio.h  声音：播放、音效、响度与频谱（D-33 批次 2）
#ifndef EASEL_AUDIO_H
#define EASEL_AUDIO_H

#include <easel/core.h>

#include <memory>

namespace easel {
namespace audio {

// 放一遍。wav / mp3 / flac 都能放，解码器是内置的，不用装任何东西。
//     easel::audio::play("data/click.wav");
// 路径不对、或者这台机器根本没声卡，都只是返回 false —— 程序不会因为没声音就崩。
bool play(const std::string& path);
// 循环播（背景音乐用这个）。边放边解码，几分钟的 mp3 也不会卡住这一帧。
bool loop(const std::string& path);
// 全停：play / loop 起的，还有学生自己 load 出来的 Sound，一起停。
void stop();

// 总音量 0..1。超出范围会被夹回去。
void   volume(double v);
double volume();

// 此刻播放出去的响度 0..1（对应 Scratch 的「响度」）。
// 注意是**放出去的**声音，不是麦克风 —— 麦克风要开采集设备和权限，D-33 决定不做。
double loudness();

// 此刻的频谱：从低频到高频 bands 个格子，每个 0..1，画成柱子就是音乐可视化。
//     for (int i = 0; i < (int)f.size(); ++i) c.rect(...f[i]...);
// 值已经做过时间平滑，柱子不会一帧一个样。
std::vector<float> spectrum(int bands = 64);

// 音频设备起来了没。第一次调用任何 audio:: 函数时才会去开设备。
bool ok();
// 一行自检：后端、设备名、采样率；开不起来就是失败原因。F12 的自检页里也有。
std::string doctor();

// 短音效反复用：load 一次，play 很多次。
//     Sound beep = easel::audio::load("data/beep.wav");
//     beep.volume(0.3);  beep.pitch(1.5);  beep.play();
// 拷贝它得到的是同一个声音（内部是共享的），随手放进 vector 里没问题。
// 一个 Sound 同一时刻只响一次：还在响的时候再 play()，会从头重放（和 Scratch 一样）。
class Sound {
public:
    // 加载失败（文件不存在、没声卡）返回一个空句柄：if (!s) 判得出来，
    // 而且对空句柄调 play/stop/volume/pitch 都是安全的空操作。
    static Sound load(const std::string& path);

    void play();
    void stop();
    void volume(double v);   // 0..1
    void pitch(double p);    // 1.0 = 原速原调，2.0 = 快一倍、高八度

    bool loop = false;       // 下一次 play() 生效

    explicit operator bool() const { return (bool)p_; }

private:
    struct Impl;
    std::shared_ptr<Impl> p_;
};

// D-33 里写的那种写法：Sound s = audio::load("data/beep.wav");
inline Sound load(const std::string& path) { return Sound::load(path); }

}  // namespace audio
}  // namespace easel
#endif
