// Easel — file.h  系统的打开 / 保存对话框
#ifndef EASEL_FILE_H
#define EASEL_FILE_H

#include <easel/core.h>

namespace easel {
namespace file {

// 弹出系统对话框。用户取消就返回空串（记得判断）。
//     std::string p = easel::file::open();
//     if (!p.empty()) project = easel::fs::loadJson(p);
std::string open(const char* filterName = "工程文件", const char* extensions = "json");
// 选一个目录。取消就返回空串。
std::string folder(const char* defaultPath = nullptr);

std::string save(const char* defaultName = "project.json", const char* filterName = "工程文件",
                 const char* extensions = "json");

}  // namespace file
}  // namespace easel
#endif
