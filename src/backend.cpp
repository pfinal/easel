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
#endif

namespace easel {
namespace internal {
namespace backend {

namespace {
GLFWwindow* g_window = nullptr;
std::string g_gpu = "未知";
bool        g_ready = false;

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
#endif

}  // namespace backend
}  // namespace internal
}  // namespace easel
