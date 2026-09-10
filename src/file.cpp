// Easel — file.cpp  系统的打开 / 保存对话框（nativefiledialog-extended）
#include "internal.h"

#include <nfd.h>

namespace easel {
namespace file {

namespace {
struct NfdGuard {
    bool ok = false;
    NfdGuard() { ok = (NFD_Init() == NFD_OKAY); }
    ~NfdGuard() { if (ok) NFD_Quit(); }
};
}  // namespace

std::string open(const char* filterName, const char* extensions) {
    NfdGuard guard;
    if (!guard.ok) {
        EASEL_WARN("打不开文件对话框：%s", NFD_GetError());
        return {};
    }
    nfdu8filteritem_t filter[1] = {{filterName, extensions}};
    nfdu8char_t*      out = nullptr;
    nfdresult_t       r = NFD_OpenDialogU8(&out, filter, 1, nullptr);
    std::string       path;
    if (r == NFD_OKAY && out) {
        path = (const char*)out;
        NFD_FreePathU8(out);
        EASEL_LOG("打开 %s", path.c_str());
    } else if (r == NFD_ERROR) {
        EASEL_WARN("文件对话框出错：%s", NFD_GetError());
    }
    return path;
}

// 选一个目录（工作台的「新建工程放哪」「打开工程」用它）
std::string folder(const char* defaultPath) {
    NfdGuard guard;
    if (!guard.ok) {
        EASEL_WARN("打不开文件对话框：%s", NFD_GetError());
        return {};
    }
    nfdu8char_t* out = nullptr;
    nfdresult_t  r = NFD_PickFolderU8(&out, defaultPath && *defaultPath ? defaultPath : nullptr);
    std::string  path;
    if (r == NFD_OKAY && out) {
        path = (const char*)out;
        NFD_FreePathU8(out);
    } else if (r == NFD_ERROR) {
        EASEL_WARN("文件对话框出错：%s", NFD_GetError());
    }
    return path;
}

std::string save(const char* defaultName, const char* filterName, const char* extensions) {
    NfdGuard guard;
    if (!guard.ok) {
        EASEL_WARN("打不开文件对话框：%s", NFD_GetError());
        return {};
    }
    nfdu8filteritem_t filter[1] = {{filterName, extensions}};
    nfdu8char_t*      out = nullptr;
    nfdresult_t       r = NFD_SaveDialogU8(&out, filter, 1, nullptr, defaultName);
    std::string       path;
    if (r == NFD_OKAY && out) {
        path = (const char*)out;
        NFD_FreePathU8(out);
        EASEL_LOG("保存到 %s", path.c_str());
    } else if (r == NFD_ERROR) {
        EASEL_WARN("文件对话框出错：%s", NFD_GetError());
    }
    return path;
}

}  // namespace file
}  // namespace easel
