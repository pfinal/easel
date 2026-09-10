// Easel — graphics.cpp  离屏画布（Graphics）
// API 说明和坐标系约定见 include/easel/canvas.h 里 Graphics 类上面那段注释。
#include "internal.h"

namespace easel {

Graphics::Graphics(Graphics&& o) noexcept
    : w_(o.w_), h_(o.h_), texId_(o.texId_), fboId_(o.fboId_), cam_(o.cam_), canvas_(o.canvas_),
      dl_(o.dl_), active_(o.active_) {
    o.w_ = o.h_ = 0;
    o.texId_ = o.fboId_ = 0;
    o.dl_ = nullptr;
    o.active_ = false;
}

Graphics& Graphics::operator=(Graphics&& o) noexcept {
    if (this != &o) {
        destroy();
        w_ = o.w_;
        h_ = o.h_;
        texId_ = o.texId_;
        fboId_ = o.fboId_;
        cam_ = o.cam_;
        canvas_ = o.canvas_;
        dl_ = o.dl_;
        active_ = o.active_;
        o.w_ = o.h_ = 0;
        o.texId_ = o.fboId_ = 0;
        o.dl_ = nullptr;
        o.active_ = false;
    }
    return *this;
}

Graphics::~Graphics() { destroy(); }

bool Graphics::create(int w, int h) {
    if (w <= 0 || h <= 0) {
        EASEL_WARN("Graphics::create 的尺寸不对（%d x %d）", w, h);
        return false;
    }
    destroy();   // 先清掉旧的（如果有）——允许重复调 create() 换个尺寸
    std::uint64_t tex = 0, fbo = 0;
    if (!internal::backend::createRenderTarget(w, h, &tex, &fbo)) {
        EASEL_WARN("离屏画布建不了（%d x %d），后端：%s", w, h, internal::backend::name());
        return false;
    }
    texId_ = tex;
    fboId_ = fbo;
    w_ = w;
    h_ = h;
    // 1:1 无缩放：world (0,0) 落在缓冲左上角，world (w,h) 落在右下角。
    cam_.setViewport(Rect(0, 0, (double)w, (double)h));
    cam_.zoom(1.0);
    cam_.center({w / 2.0, h / 2.0});
    cam_.panZoom(false);   // 这台相机不是给鼠标滚轮/拖拽用的
    if (!dl_) dl_ = new ImDrawList(ImGui::GetDrawListSharedData());
    clear();   // 显存里刚分配出来的纹理内容是未定义的，先清成全透明
    EASEL_LOG("离屏画布已建（%d x %d）", w, h);
    return true;
}

void Graphics::destroy() {
    if (active_) {
        EASEL_WARN("Graphics 在还没 end() 的时候被销毁了，先帮你 end() 一次");
        end();
    }
    if (texId_ || fboId_) internal::backend::destroyRenderTarget(texId_, fboId_);
    texId_ = fboId_ = 0;
    w_ = h_ = 0;
    if (dl_) {
        delete (ImDrawList*)dl_;
        dl_ = nullptr;
    }
}

void Graphics::clear(const Color& c) {
    if (!valid()) return;
    EASEL_CHECK(!active_, "Graphics::clear() 不能在 begin()/end() 之间调用");
    if (!internal::backend::beginRenderTarget(fboId_, w_, h_)) return;
    internal::backend::clearRenderTarget(c);
    internal::backend::endRenderTarget();
}

Canvas& Graphics::begin() {
    EASEL_CHECK(!internal::shared().canvasRecording,
                "Graphics::begin() 不能在 onDraw 里调用——那时主画布正在录制，"
                "离屏画布只能在 onStart / onFrame 里 begin()/end()");
    EASEL_CHECK(!active_, "Graphics::begin() 之前忘了 end()——同一个 Graphics 不能连续 begin() 两次");
    if (!valid()) return canvas_;   // 没建成功：Canvas 内部 dl_ 仍是空，画什么都静默丢掉
    active_ = true;
    ImDrawList* dl = (ImDrawList*)dl_;
    // 照 ImGui 自己「后台绘制列表」的初始化方式来：重置 + 推一次纹理和裁剪矩形，
    // 之后 AddLine/AddRectFilled 这些图元命令才有地方落（否则 _CmdHeader 是空的）。
    dl->_ResetForNewFrame();
    dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
    dl->PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)w_, (float)h_), false);
    canvas_.begin(cam_, dl_, /*hovered=*/false, /*dpiScale=*/1.0f);
    return canvas_;
}

void Graphics::end() {
    if (!active_) return;
    active_ = false;
    canvas_.end();
    ImDrawList* dl = (ImDrawList*)dl_;
    dl->PopClipRect();
    dl->PopTexture();
    if (!valid()) return;
    // 把这一帧画的东西真正烧进纹理：绑 FBO，把刚攒的 drawlist 交给 ImGui 的渲染
    // 后端画一遍，再把 FBO/viewport 还原——见 backend.cpp 的 renderDrawList 注释。
    if (!internal::backend::beginRenderTarget(fboId_, w_, h_)) return;
    internal::backend::renderDrawList(dl_, w_, h_);
    internal::backend::endRenderTarget();
}

Texture Graphics::texture() const {
    Texture t;
    t.id = texId_;
    t.w = w_;
    t.h = h_;
    return t;
}

}  // namespace easel
