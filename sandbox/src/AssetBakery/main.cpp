/**
 * @file main.cpp
 * @brief Asset Bakery — 离线资产烹饪工具
 *
 * 将运行时资产预处理为优化的二进制格式，实现零开销加载。
 *
 * 使用方式：
 *   AssetBakery --shaders assets/shaders/ output/baked/
 *   AssetBakery --mesh assets/models/test.obj output/baked/
 *   AssetBakery --material assets/materials/ output/baked/
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════
// 烘焙器基类
// ════════════════════════════════════════════════════════════

class Baker {
public:
    virtual ~Baker() = default;
    virtual bool Bake(const std::string& inputPath, const std::string& outputPath) = 0;
    virtual const char* GetName() const = 0;

    bool ProcessDirectory(const std::string& inputDir, const std::string& outputDir) {
        if (!fs::exists(inputDir)) {
            std::fprintf(stderr, "[%s] Input directory not found: %s\n", GetName(), inputDir.c_str());
            return false;
        }
        fs::create_directories(outputDir);

        bool allOk = true;
        for (const auto& entry : fs::directory_iterator(inputDir)) {
            if (entry.is_regular_file()) {
                std::string outPath = outputDir + "/" + entry.path().filename().string() + ".baked";
                if (!Bake(entry.path().string(), outPath)) {
                    std::fprintf(stderr, "[%s] FAILED: %s\n", GetName(), entry.path().filename().string().c_str());
                    allOk = false;
                } else {
                    std::printf("[%s] OK: %s -> %s\n", GetName(),
                                entry.path().filename().string().c_str(), outPath.c_str());
                }
            }
        }
        return allOk;
    }
};

// ════════════════════════════════════════════════════════════
// 着色器烘焙器
// ════════════════════════════════════════════════════════════

class ShaderBaker : public Baker {
public:
    const char* GetName() const override { return "ShaderBaker"; }

    bool Bake(const std::string& inputPath, const std::string& outputPath) override {
        // 读取原始 GLSL 源码
        std::ifstream inFile(inputPath, std::ios::binary | std::ios::ate);
        if (!inFile.is_open()) return false;

        size_t fileSize = (size_t)inFile.tellg();
        inFile.seekg(0);
        std::vector<char> source(fileSize);
        inFile.read(source.data(), fileSize);
        inFile.close();

        // 二进制格式:
        //   [4 bytes] magic = "SHD\0"
        //   [16 bytes] MD5 hash of source (版本指纹)
        //   [4 bytes] source_size
        //   [N bytes] GLSL source
        //   [4 bytes] reflection_data_size (0 = 无反射)
        //   [N bytes] reflection_data (SPIRV-Cross 序列化, 可选)

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) return false;

        const uint32_t magic = 0x00444853; // "SHD\0"
        outFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

        // 简单的版本指纹: 使用文件大小 + 前 100 字节 + 后 100 字节的异或和
        unsigned char fingerprint[16] = {};
        fingerprint[0] = (fileSize >> 0)  & 0xFF;
        fingerprint[1] = (fileSize >> 8)  & 0xFF;
        fingerprint[2] = (fileSize >> 16) & 0xFF;
        fingerprint[3] = (fileSize >> 24) & 0xFF;
        for (size_t i = 0; i < fileSize && i < 100; ++i)
            fingerprint[4 + (i % 12)] ^= source[i];
        for (size_t i = (fileSize > 100 ? fileSize - 100 : 0); i < fileSize; ++i)
            fingerprint[4 + ((i + 7) % 12)] ^= source[i];
        outFile.write(reinterpret_cast<const char*>(fingerprint), 16);

        uint32_t srcSize = static_cast<uint32_t>(fileSize);
        outFile.write(reinterpret_cast<const char*>(&srcSize), sizeof(srcSize));
        outFile.write(source.data(), fileSize);

        // 无反射数据
        uint32_t reflectSize = 0;
        outFile.write(reinterpret_cast<const char*>(&reflectSize), sizeof(reflectSize));

        return true;
    }
};

// ════════════════════════════════════════════════════════════
// 材质模板烘焙器
// ════════════════════════════════════════════════════════════

class MaterialBaker : public Baker {
public:
    const char* GetName() const override { return "MaterialBaker"; }

    bool Bake(const std::string& inputPath, const std::string& outputPath) override {
        // 读取材质模板描述文件（JSON 格式简化版）
        std::ifstream inFile(inputPath);
        if (!inFile.is_open()) return false;

        std::stringstream ss;
        ss << inFile.rdbuf();
        std::string content = ss.str();
        inFile.close();

        // 二进制格式:
        //   [4 bytes] magic = "MAT\0"
        //   [4 bytes] content_size
        //   [N bytes] serialized material data

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) return false;

        const uint32_t magic = 0x0054414D; // "MAT\0"
        outFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

        uint32_t contentSize = static_cast<uint32_t>(content.size());
        outFile.write(reinterpret_cast<const char*>(&contentSize), sizeof(contentSize));
        outFile.write(content.data(), content.size());

        return true;
    }
};

// ════════════════════════════════════════════════════════════
// gp01 场景 fixture 生成器
// ════════════════════════════════════════════════════════════

#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <nlohmann/json.hpp>

#include <unordered_map>

/// gp01 清单的 path → GUID 索引。
///
/// GUID 一律从**已入库的** manifest.json 解析，不在此处硬编码：清单是资产
/// GUID 的唯一真理源，硬编码会在清单重排后静默指向错误资产。注意必须用
/// FromHex 而非 FromString —— 清单里的 GUID 是 32 位无连字符裸 hex，
/// FromString 只认 8-4-4-4-12 的带连字符形式，对裸 hex 会**静默返回 Null**，
/// 而 Null GUID 在加载期只产生一条 warning，实体会无 sprite/script 正常载入。
/// 那是一条不会让任何测试变红、却让整场游戏没有贴图的静默降级。
class ManifestGuidIndex {
public:
    bool Load(const std::string& manifestPath) {
        std::ifstream inFile(manifestPath);
        if (!inFile.is_open()) {
            std::fprintf(stderr, "[Gp01Scene] manifest not found: %s\n", manifestPath.c_str());
            return false;
        }

        nlohmann::json j;
        try {
            inFile >> j;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[Gp01Scene] manifest parse failed: %s\n", e.what());
            return false;
        }

        if (!j.contains("assets") || !j["assets"].is_array()) {
            std::fprintf(stderr, "[Gp01Scene] manifest has no 'assets' array\n");
            return false;
        }

        for (const auto& a : j["assets"]) {
            if (!a.contains("guid") || !a.contains("path")) continue;
            m_ByPath[a["path"].get<std::string>()] = a["guid"].get<std::string>();
        }
        std::printf("[Gp01Scene] manifest indexed: %zu asset(s)\n", m_ByPath.size());
        return true;
    }

    /// fail-fast：清单里没有就直接失败，绝不退回 Null GUID。
    bool Resolve(const std::string& path, Engine::ResourceGUID& out) const {
        auto it = m_ByPath.find(path);
        if (it == m_ByPath.end()) {
            std::fprintf(stderr, "[Gp01Scene] asset not in manifest: %s\n", path.c_str());
            return false;
        }
        out = Engine::ResourceGUID::FromHex(it->second);
        if (out.IsNull()) {
            std::fprintf(stderr, "[Gp01Scene] unparseable guid for %s: %s\n",
                         path.c_str(), it->second.c_str());
            return false;
        }
        return true;
    }

private:
    std::unordered_map<std::string, std::string> m_ByPath;
};

/// gp01 竞技场的一份实体表。
///
/// 这张表是**转录**自 provisioned 工作区里的那份 `assets/gp01/Main.scene`，
/// 不是新设计的内容。之所以转录而非重新设计：Pad_*/Wall_* 的坐标构成关卡的
/// 碰撞与玩法几何，`game.lua` 与 test_gp01 的战斗断言依赖它们；改动坐标
/// 等于悄悄改玩法。因此这里逐值保留，并把「这是 fixture 的可追溯来源」
/// 写进注释 —— 生成器复现的是既有内容，不是一个新场景。
struct Gp01Entity {
    const char* name;
    float x, y, z;
    const char* spritePath;   // nullptr = 无 sprite
    const char* scriptPath;   // nullptr = 无 script
};

static const Gp01Entity kGp01Entities[] = {
    { "Director",  0.0f,  0.0f,   0.0f, nullptr,                   "assets/gp01/game.lua" },
    { "Player",    0.0f,  0.0f,   4.0f, "assets/gp01/tex/player.png", nullptr },
    { "Pad_N",     0.0f,  0.0f,  -9.5f, "assets/gp01/tex/pad.png",    nullptr },
    { "Pad_S",     0.0f,  0.0f,  -1.5f, "assets/gp01/tex/pad.png",    nullptr },
    { "Pad_W",    -7.5f,  0.0f,  -5.5f, "assets/gp01/tex/pad.png",    nullptr },
    { "Pad_E",     7.5f,  0.0f,  -5.5f, "assets/gp01/tex/pad.png",    nullptr },
    { "Wall_N",    0.0f,  0.0f, -11.0f, "assets/gp01/tex/wall.png",   nullptr },
    { "Wall_S",    0.0f,  0.0f,   6.0f, "assets/gp01/tex/wall.png",   nullptr },
    { "Wall_W",   -9.0f,  0.0f,  -2.5f, "assets/gp01/tex/wall.png",   nullptr },
    { "Wall_E",    9.0f,  0.0f,  -2.5f, "assets/gp01/tex/wall.png",   nullptr },
};

/// 用引擎自己的 SceneSerializerV1 落盘 —— 不手写 JSON，避免格式与加载器漂移。
static bool GenerateGp01Scene(const std::string& manifestPath, const std::string& outputPath) {
    ManifestGuidIndex index;
    if (!index.Load(manifestPath)) return false;

    Engine::Content::SceneSnapshot snap;
    snap.version = 1;

    for (const auto& e : kGp01Entities) {
        Engine::Content::SerializedEntity se;
        se.name = e.name;
        se.px = e.x;
        se.py = e.y;
        se.pz = e.z;
        if (e.spritePath && !index.Resolve(e.spritePath, se.spriteGuid)) return false;
        if (e.scriptPath && !index.Resolve(e.scriptPath, se.scriptGuid)) return false;
        snap.entities.push_back(std::move(se));
    }

    if (!Engine::Content::SaveSnapshotToFile(snap, outputPath)) {
        std::fprintf(stderr, "[Gp01Scene] SaveSnapshotToFile failed: %s\n", outputPath.c_str());
        return false;
    }

    std::printf("[Gp01Scene] wrote %zu entities -> %s\n", snap.entities.size(), outputPath.c_str());
    return true;
}

// ════════════════════════════════════════════════════════════
// 入口
// ════════════════════════════════════════════════════════════

static void PrintUsage() {
    std::printf(
        "Usage: AssetBakery --type <shaders|materials> <input_dir> <output_dir>\n"
        "       AssetBakery --gp01-scene <manifest.json> <output.scene>\n"
        "\n"
        "Options:\n"
        "  --shaders     Bake GLSL shaders to .shader_bin format\n"
        "  --materials   Bake material templates to .mat_bin format\n"
        "  --gp01-scene  Regenerate the gp01 scene fixture from the tracked manifest.\n"
        "                The .scene files are gitignored (*.scene), so a clean\n"
        "                checkout has no scene; run this before any test or gate\n"
        "                that opens assets/gp01.\n"
        "  --help        Show this help\n"
    );
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        PrintUsage();
        return 1;
    }

    std::string type = argv[1];
    std::string inputDir = argv[2];
    std::string outputDir = argv[3];

    bool result = false;

    if (type == "--shaders") {
        ShaderBaker baker;
        result = baker.ProcessDirectory(inputDir, outputDir);
    } else if (type == "--materials") {
        MaterialBaker baker;
        result = baker.ProcessDirectory(inputDir, outputDir);
    } else if (type == "--gp01-scene") {
        result = GenerateGp01Scene(inputDir, outputDir);
    } else {
        std::fprintf(stderr, "Unknown type: %s\n", type.c_str());
        PrintUsage();
        return 1;
    }

    return result ? 0 : 1;
}