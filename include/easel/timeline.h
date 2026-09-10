// Easel — timeline.h  「算完 + 回放」（D-09）
#ifndef EASEL_TIMELINE_H
#define EASEL_TIMELINE_H

#include <easel/core.h>

namespace easel {

// 播放控制的公共部分。App::transport() 收的是它，所以帧的类型可以随便。
class TimelineBase {
public:
    virtual ~TimelineBase() = default;
    virtual size_t size() const = 0;

    void   play() { playing_ = size() > 0; }
    void   pause() { playing_ = false; }
    void   toggle() { playing_ ? pause() : play(); }
    bool   playing() const { return playing_; }
    void   step(int d = 1) { pause(); seek(index() + d); }
    void   seek(int i);
    int    index() const { return (int)pos_; }
    void   speed(double s) { speed_ = clamp(s, 0.05, 64.0); }
    double speed() const { return speed_; }
    void   fps(double f) { fps_ = clamp(f, 1.0, 240.0); }
    double fps() const { return fps_; }
    void   rewind() { pos_ = 0; }
    bool   atEnd() const { return size() == 0 || index() >= (int)size() - 1; }
    double progress() const { return size() <= 1 ? 0.0 : (double)index() / (double)(size() - 1); }

    bool loop = false;

    // Easel 内部每帧调用
    void update(double dt);

protected:
    double pos_ = 0;
    bool   playing_ = false;
    double speed_ = 1.0;
    double fps_ = 30.0;
};

// Timeline<Frame> tl;  tl.load(solve(project));  app.transport(tl);
// 然后在 onDraw 里画 tl.current() 就行。
template <class T>
class Timeline : public TimelineBase {
public:
    void load(std::vector<T> frames) {
        frames_ = std::move(frames);
        pos_ = 0;
        playing_ = false;
    }
    void   push(const T& f) { frames_.push_back(f); }
    void   clear() { frames_.clear(); pos_ = 0; playing_ = false; }
    size_t size() const override { return frames_.size(); }
    bool   empty() const { return frames_.empty(); }

    const T& current() const { return at(index()); }
    const T& at(int i) const {
        static T fallback{};
        if (frames_.empty()) return fallback;
        if (i < 0) i = 0;
        if (i >= (int)frames_.size()) i = (int)frames_.size() - 1;
        return frames_[(size_t)i];
    }
    const std::vector<T>& frames() const { return frames_; }
    std::vector<T>&       frames() { return frames_; }

private:
    std::vector<T> frames_;
};

}  // namespace easel
#endif
