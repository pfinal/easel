// Easel — texture.cpp  读图片（底图）
#include "internal.h"

// stb 是别人的代码，它的警告不该出现在我们的构建输出里
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#elif defined(_MSC_VER)
#  pragma warning(push, 0)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace easel {

Texture loadTexture(const std::string& path, bool pixelated) {
    Texture t;
    t.path = path;
    if (!fs::exists(path)) {
        EASEL_WARN("图片不存在：%s（当前目录 %s）", path.c_str(), fs::cwd().c_str());
        return t;
    }
    int            w = 0, h = 0, channels = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!px) {
        EASEL_WARN("图片读不了：%s（%s）", path.c_str(), stbi_failure_reason());
        return t;
    }
    std::uint64_t id = 0;
    bool          ok = internal::backend::createTexture(px, w, h, &id, pixelated);
    stbi_image_free(px);
    if (!ok) {
        EASEL_WARN("图片上传显卡失败：%s", path.c_str());
        return t;
    }
    t.id = id;
    t.w = w;
    t.h = h;
    EASEL_LOG("载入图片 %s（%d x %d）", path.c_str(), w, h);
    return t;
}

bool App::screenshot(const std::string& path) {
    int                        w = 0, h = 0;
    std::vector<unsigned char> px;
    if (!internal::backend::readPixels(&w, &h, &px)) return false;
    std::string dir = fs::dirOf(path);
    if (!dir.empty()) fs::makeDirs(dir);
    if (!stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4)) {
        EASEL_WARN("截图写不了：%s", path.c_str());
        return false;
    }
    EASEL_LOG("截图已保存 %s（%d x %d）", path.c_str(), w, h);
    return true;
}

void freeTexture(Texture& t) {
    if (t.id) internal::backend::destroyTexture(t.id);
    t.id = 0;
    t.w = t.h = 0;
}

}  // namespace easel
