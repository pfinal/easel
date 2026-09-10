// Easel — backend.cpp  窗口 + 渲染后端
//
// v0.1.0：三平台默认 OpenGL 3（老师在 Mac 上验过）。
// Windows 的 DX11 走 -DEASEL_BACKEND=dx11 打开；等 9/21 的 Windows 检查点实测通过
// 之后再把它设成 Windows 的默认（D-04 的计划）。
#include "internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#if defined(EASEL_BACKEND_DX11)
#  include <imgui_impl_dx11.h>
#  include <d3d11.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#else
#  include <imgui_impl_opengl3.h>
#  if defined(__APPLE__)
#    define GL_SILENCE_DEPRECATION
#    include <OpenGL/gl3.h>
#  elif defined(_WIN32)
#    include <windows.h>
#    include <GL/gl.h>
#  else
#    include <GL/gl.h>
#  endif
#  ifndef GL_CLAMP_TO_EDGE
#    define GL_CLAMP_TO_EDGE 0x812F
#  endif
// ---- 离屏渲染目标（Graphics）要用到的 FBO 常量 ----
// GL_RGBA8 是 GL 1.1 就有的内部格式 token，Windows 的 <GL/gl.h> 也认得；
// 下面这四个是 GL_ARB_framebuffer_object（GL 3.0 起并入核心）的东西，
// macOS 的 <OpenGL/gl3.h> 已经声明了，Windows/Linux 的旧 <GL/gl.h> 没有。
#  ifndef GL_FRAMEBUFFER
#    define GL_FRAMEBUFFER 0x8D40
#  endif
#  ifndef GL_COLOR_ATTACHMENT0
#    define GL_COLOR_ATTACHMENT0 0x8CE0
#  endif
#  ifndef GL_FRAMEBUFFER_COMPLETE
#    define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#  endif
#  ifndef GL_FRAMEBUFFER_BINDING
#    define GL_FRAMEBUFFER_BINDING 0x8CA6
#  endif
#  if defined(__APPLE__)
// macOS 的核心头文件已经声明了真正的函数原型，直接用。
#  else
// Windows 的 <GL/gl.h> 停在 1.1，FBO 系列函数得像 GLEW/GLAD 那样自己用
// glfwGetProcAddress 拿函数指针——只差这五个，不值得为此引入一整个 loader 依赖。
#    if defined(_WIN32)
#      define EASEL_GLAPI __stdcall
#    else
#      define EASEL_GLAPI
#    endif
typedef void   (EASEL_GLAPI *EaselPFNGenFramebuffers)(GLsizei, GLuint*);
typedef void   (EASEL_GLAPI *EaselPFNBindFramebuffer)(GLenum, GLuint);
typedef void   (EASEL_GLAPI *EaselPFNFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (EASEL_GLAPI *EaselPFNCheckFramebufferStatus)(GLenum);
typedef void   (EASEL_GLAPI *EaselPFNDeleteFramebuffers)(GLsizei, const GLuint*);
#  endif
#endif

namespace easel {
namespace internal {
namespace backend {

namespace {
GLFWwindow* g_window = nullptr;
std::string g_gpu = "未知";
bool        g_ready = false;

#if !defined(EASEL_BACKEND_DX11)
// beginRenderTarget/endRenderTarget 存/还原的现场——只支持单层，Graphics 不会嵌套用它。
GLint g_rtSavedFbo = 0;
GLint g_rtSavedViewport[4] = {0, 0, 0, 0};
#endif

#if !defined(EASEL_BACKEND_DX11) && !defined(__APPLE__)
// Windows / Linux 的 FBO 函数指针，第一次用到时用 glfwGetProcAddress 加载一次。
EaselPFNGenFramebuffers        glGenFramebuffers_ = nullptr;
EaselPFNBindFramebuffer        glBindFramebuffer_ = nullptr;
EaselPFNFramebufferTexture2D   glFramebufferTexture2D_ = nullptr;
EaselPFNCheckFramebufferStatus glCheckFramebufferStatus_ = nullptr;
EaselPFNDeleteFramebuffers     glDeleteFramebuffers_ = nullptr;
bool                           g_fboFnsTried = false;
bool                           g_fboFnsOk = false;

bool loadFboFunctions() {
    if (g_fboFnsTried) return g_fboFnsOk;
    g_fboFnsTried = true;
    glGenFramebuffers_ = (EaselPFNGenFramebuffers)glfwGetProcAddress("glGenFramebuffers");
    glBindFramebuffer_ = (EaselPFNBindFramebuffer)glfwGetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D_ = (EaselPFNFramebufferTexture2D)glfwGetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus_ = (EaselPFNCheckFramebufferStatus)glfwGetProcAddress("glCheckFramebufferStatus");
    glDeleteFramebuffers_ = (EaselPFNDeleteFramebuffers)glfwGetProcAddress("glDeleteFramebuffers");
    g_fboFnsOk = glGenFramebuffers_ && glBindFramebuffer_ && glFramebufferTexture2D_ &&
                 glCheckFramebufferStatus_ && glDeleteFramebuffers_;
    return g_fboFnsOk;
}
#elif !defined(EASEL_BACKEND_DX11)
// macOS：<OpenGL/gl3.h> 已经声明了真正的函数，直接把名字接到真实符号上，
// 调用点（下面 createRenderTarget 等）就不用再分平台写两套。
inline bool loadFboFunctions() { return true; }
#  define glGenFramebuffers_ glGenFramebuffers
#  define glBindFramebuffer_ glBindFramebuffer
#  define glFramebufferTexture2D_ glFramebufferTexture2D
#  define glCheckFramebufferStatus_ glCheckFramebufferStatus
#  define glDeleteFramebuffers_ glDeleteFramebuffers
#endif

void glfwErrorCallback(int code, const char* desc) {
    EASEL_ERROR("GLFW 错误 %d: %s", code, desc ? desc : "?");
}
}  // namespace

const char* name() {
#if defined(EASEL_BACKEND_DX11)
    return "DirectX 11";
#else
    return "OpenGL 3";
#endif
}

std::string gpu() { return g_gpu; }

#if defined(EASEL_BACKEND_DX11)
// ---------------------------------------------------------------- DX11
namespace {
ID3D11Device*           g_dev = nullptr;
ID3D11DeviceContext*    g_ctx = nullptr;
IDXGISwapChain*         g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}
void releaseRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}
}  // namespace

bool init(const char* title, int w, int h, bool visible, GLFWwindow** outWindow) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // DX11 自己管理绘制
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    ImGui::CreateContext();
    ImPlot::CreateContext();
    float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    g_window = glfwCreateWindow((int)(w * scale), (int)(h * scale), title, nullptr, nullptr);
    if (!g_window) { glfwTerminate(); return false; }

    HWND                 hwnd = glfwGetWin32Window(g_window);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                               2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // 没有独显 / 远程桌面时退到 WARP 软件渲染
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) {
        EASEL_ERROR("创建 D3D11 设备失败 (0x%08lX)", (unsigned long)hr);
        return false;
    }
    createRenderTarget();
    g_gpu = (got == D3D_FEATURE_LEVEL_11_0) ? "D3D11 硬件" : "D3D10 / WARP";

    ImGui_ImplGlfw_InitForOther(g_window, true);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_ready = true;
    if (outWindow) *outWindow = g_window;
    return true;
}

void newFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void present(GLFWwindow* win, const Color& clear) {
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(win, &w, &h);
    static int lastW = 0, lastH = 0;
    if (w > 0 && h > 0 && (w != lastW || h != lastH)) {
        releaseRenderTarget();
        g_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
        createRenderTarget();
        lastW = w; lastH = h;
    }
    const float c[4] = {clear.r, clear.g, clear.b, clear.a};
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, c);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
}

void shutdown() {
    g_ready = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    releaseRenderTarget();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_window) glfwDestroyWindow(g_window);
    glfwTerminate();
}

bool createTexture(const unsigned char* rgba, int w, int h, std::uint64_t* outId,
                    bool pixelated) {
    if (!g_ready) {
        EASEL_ERROR("显卡还没准备好，图片加载不了。把 loadTexture(...) 放进 app.onStart([]{ ... }) 里。");
        return false;
    }
    // DX11 下暂不支持按贴图切换过滤方式：这里只建 ShaderResourceView，采样器
    // 由 ImGui 的 DX11 impl（imgui_impl_dx11.cpp）统一创建、所有贴图共用一个，
    // 改它得动 ImGui 的实现文件，不在这次改动范围内——pixelated 暂时退回线性。
    (void)pixelated;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = rgba;
    sub.SysMemPitch = (UINT)(w * 4);
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, &sub, &tex)) || !tex) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = d.Format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = g_dev->CreateShaderResourceView(tex, &sv, &srv);
    tex->Release();
    if (FAILED(hr) || !srv) return false;
    *outId = (std::uint64_t)(uintptr_t)srv;
    return true;
}

void destroyTexture(std::uint64_t id) {
    if (id) ((ID3D11ShaderResourceView*)(uintptr_t)id)->Release();
}

bool readPixels(int*, int*, std::vector<unsigned char>*) {
    EASEL_WARN("DX11 后端还不支持截图（v0.2）。用 -DEASEL_BACKEND=gl 构建可以截。");
    return false;
}

// ---------------------------------------------------------------- 离屏渲染目标（已知缺口）
// D3D11 版的 Graphics 需要一块 ID3D11Texture2D（D3D11_BIND_RENDER_TARGET）+
// ID3D11RenderTargetView，再把 ImGui_ImplDX11_RenderDrawData 指过去画——工作量和
// GL 分支差不多，但这次改动没有 DX11 的实测环境（D-04：DX11 要等 9/21 检查点才验），
// 不实测就把这段接上风险太大，所以先老实返回 false。学生在 DX11 后端下想要「积累」
// 效果，改用 Layer（重放开销更大，但至少画得对）。
bool createRenderTarget(int, int, std::uint64_t*, std::uint64_t*) {
    EASEL_WARN("DX11 后端暂不支持离屏画布（Graphics），改用 Layer。");
    return false;
}
void destroyRenderTarget(std::uint64_t, std::uint64_t) {}
bool beginRenderTarget(std::uint64_t, int, int) { return false; }
void endRenderTarget() {}
void clearRenderTarget(const Color&) {}
bool renderDrawList(void*, int, int) { return false; }

#else
// ---------------------------------------------------------------- OpenGL 3
bool init(const char* title, int w, int h, bool visible, GLFWwindow** outWindow) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        EASEL_ERROR("glfwInit 失败：这台机器可能没有可用的图形环境");
        return false;
    }
#if defined(__APPLE__)
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);

    ImGui::CreateContext();
    ImPlot::CreateContext();

    float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    if (scale <= 0.f) scale = 1.f;
    g_window = glfwCreateWindow((int)(w * scale), (int)(h * scale), title, nullptr, nullptr);
    if (!g_window) {
        EASEL_ERROR("创建窗口失败：显卡驱动可能不支持 OpenGL 3。Windows 上可以试 -DEASEL_BACKEND=dx11");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* rend = (const char*)glGetString(GL_RENDERER);
    const char* ver = (const char*)glGetString(GL_VERSION);
    g_gpu = std::string(rend ? rend : "?") + " / " + (vendor ? vendor : "?") + " / GL " + (ver ? ver : "?");

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init(glsl);
    g_ready = true;
    if (outWindow) *outWindow = g_window;
    return true;
}

void newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void present(GLFWwindow* win, const Color& clear) {
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(win, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);
}

void shutdown() {
    g_ready = false;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (g_window) glfwDestroyWindow(g_window);
    glfwTerminate();
}

bool createTexture(const unsigned char* rgba, int w, int h, std::uint64_t* outId,
                    bool pixelated) {
    if (!g_ready) {
        EASEL_ERROR("显卡还没准备好，图片加载不了。把 loadTexture(...) 放进 app.onStart([]{ ... }) 里。");
        return false;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return false;
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint filter = pixelated ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    *outId = (std::uint64_t)tex;
    return true;
}

void destroyTexture(std::uint64_t id) {
    GLuint t = (GLuint)id;
    if (t) glDeleteTextures(1, &t);
}

bool readPixels(int* w, int* h, std::vector<unsigned char>* rgba) {
    if (!g_window) return false;
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(g_window, &fw, &fh);
    if (fw <= 0 || fh <= 0) return false;
    rgba->assign((size_t)fw * fh * 4, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, rgba->data());
    // OpenGL 的原点在左下角，图片格式的原点在左上角，翻一下
    std::vector<unsigned char> row((size_t)fw * 4);
    for (int y = 0; y < fh / 2; ++y) {
        unsigned char* a = rgba->data() + (size_t)y * fw * 4;
        unsigned char* b = rgba->data() + (size_t)(fh - 1 - y) * fw * 4;
        std::memcpy(row.data(), a, row.size());
        std::memcpy(a, b, row.size());
        std::memcpy(b, row.data(), row.size());
    }
    *w = fw; *h = fh;
    return true;
}

// ---------------------------------------------------------------- 离屏渲染目标（Graphics）
bool createRenderTarget(int w, int h, std::uint64_t* outTexId, std::uint64_t* outFboId) {
    if (!g_ready) {
        EASEL_ERROR("显卡还没准备好，离屏画布建不了。把 Graphics::create(...) 放进 "
                    "app.onStart([]{ ... }) 或 onFrame 里。");
        return false;
    }
    if (w <= 0 || h <= 0) return false;
    if (!loadFboFunctions()) {
        EASEL_ERROR("这块显卡/驱动拿不到 FBO 函数（glGenFramebuffers），离屏画布建不了。");
        return false;
    }

    GLint prevTex = 0, prevFbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // 颜色贴图：RGBA8，线性过滤——图片放大贴到主画布上时不会一格一格发糊。
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return false;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    GLuint fbo = 0;
    glGenFramebuffers_(1, &fbo);
    if (!fbo) { glDeleteTextures(1, &tex); return false; }
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum status = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    glBindFramebuffer_(GL_FRAMEBUFFER, (GLuint)prevFbo);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        EASEL_ERROR("FBO 建不完整（状态 0x%04X），离屏画布建不了。", (unsigned)status);
        glDeleteFramebuffers_(1, &fbo);
        glDeleteTextures(1, &tex);
        return false;
    }
    *outTexId = (std::uint64_t)tex;
    *outFboId = (std::uint64_t)fbo;
    return true;
}

void destroyRenderTarget(std::uint64_t texId, std::uint64_t fboId) {
    if (fboId && loadFboFunctions()) {
        GLuint f = (GLuint)fboId;
        glDeleteFramebuffers_(1, &f);
    }
    if (texId) {
        GLuint t = (GLuint)texId;
        glDeleteTextures(1, &t);
    }
}

// 绑 FBO、把 viewport 设成整块缓冲的大小，把当前 FBO/viewport 记下来给 endRenderTarget() 还原。
// 只支持单层（不嵌套）——Graphics::begin()/end() 之间不会再嵌一层别的渲染目标。
bool beginRenderTarget(std::uint64_t fboId, int w, int h) {
    if (!g_ready || !fboId) return false;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_rtSavedFbo);
    glGetIntegerv(GL_VIEWPORT, g_rtSavedViewport);
    glBindFramebuffer_(GL_FRAMEBUFFER, (GLuint)fboId);
    glViewport(0, 0, w, h);
    return true;
}

void endRenderTarget() {
    glBindFramebuffer_(GL_FRAMEBUFFER, (GLuint)g_rtSavedFbo);
    glViewport(g_rtSavedViewport[0], g_rtSavedViewport[1], g_rtSavedViewport[2], g_rtSavedViewport[3]);
}

void clearRenderTarget(const Color& c) {
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

// 把一个（游离于 ImGui 正常帧之外的）ImDrawList 画进当前绑定的渲染目标——
// beginRenderTarget() 已经绑好 FBO、设好 viewport，这里只管拼一份只含这一个 list 的
// ImDrawData 交给 ImGui 自己的 OpenGL3 渲染后端。ImGui_ImplOpenGL3_RenderDrawData 会
// 备份/还原几乎所有 GL 状态（program/texture/blend/viewport/scissor/vao/vbo……），
// 但**不**碰 FBO 绑定——所以它画完之后颜色仍然落在我们绑的这块 FBO 上，
// beginRenderTarget/endRenderTarget 只需要管 FBO 和 viewport 这两件事。
bool renderDrawList(void* dl, int w, int h) {
    if (!g_ready || !dl) return false;
    ImDrawList* list = (ImDrawList*)dl;
    ImDrawData  dd;
    dd.DisplayPos = ImVec2(0.f, 0.f);
    dd.DisplaySize = ImVec2((float)w, (float)h);
    dd.FramebufferScale = ImVec2(1.f, 1.f);
    dd.AddDrawList(list);                                    // 处理 _PopUnusedDrawCmd + 顶点计数
    dd.Valid = true;
    dd.Textures = &ImGui::GetPlatformIO().Textures;           // 字体贴图第一次用到时在这里也能被建出来
    ImGui_ImplOpenGL3_RenderDrawData(&dd);
    return true;
}
#endif

}  // namespace backend
}  // namespace internal
}  // namespace easel
