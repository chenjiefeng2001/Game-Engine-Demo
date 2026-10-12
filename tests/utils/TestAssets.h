/**
 * @file TestAssets.h
 * @brief 定位仓库根下的 assets/ 资源，供需要真实文件 I/O 的用例使用
 *
 * 为什么需要：CI 里 ctest 的工作目录是**构建输出目录**，不是仓库根。
 * 因此形如 "assets/scenes/x.scene" 的相对路径在 CI 上解析不到，文件打不开，
 * 而在开发者机器上恰好能工作（取决于当时的工作目录与残留产物）。
 * GL46DeviceTest.cpp 里的 ResolveShaderPath 早就为 shader 解决过同一个问题，
 * 这里把同样的做法扩展到全部测试资产。
 *
 * 注意适用范围：**只有真正做文件 I/O 的调用点**才该用它，例如
 * LoadManifest / LoadSnapshotFromFile。而 Import("assets/...") 传给
 * ContentRegistry 的是**注册表内容**，用例会以字符串形式断言它
 * （EXPECT_EQ(s2.ResolvePath(tex), "assets/textures/test.png")），
 * 那种地方必须保持仓库相对写法，改了反而会让断言失真。
 *
 * 找不到时原样返回传入的相对路径，让用例以原本的信息失败，
 * 而不是悄悄换成一个不存在的绝对路径。
 */
#pragma once

#include <filesystem>
#include <string>

inline std::string ResolveTestAsset(const char* relative)
{
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        std::filesystem::path candidate = dir / relative;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        dir = dir.parent_path();
    }
    return std::string(relative);
}