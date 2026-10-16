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
// dogfood 场景 fixture 生成器
// ════════════════════════════════════════════════════════════

/// dogfood 清单里的一条资产。
///
/// 与 gp01 不同，这里的 GUID 是 fixture 自身的标识（写死在场景与清单里、
/// 互为引用），不是从资产注册表派生的，所以按字面量给出。
struct DogfoodAsset {
    const char* guid;
    const char* path;
    const char* type;   // "Script" | "Texture"
};

/// 实体引用资产时用**下标**而不是 GUID 字符串，避免同一串十六进制在表里
/// 抄错却看不出来：下标错了会在下面 resolve 阶段报「未定义下标」而 fail-fast。
struct DogfoodEntity {
    const char* name;
    float x, y, z;
    int spriteAsset;   // -1 = 无 sprite
    int scriptAsset;   // -1 = 无 script
};

struct DogfoodSpec {
    const char* id;                          // "dogfood03"
    const DogfoodAsset* assets;
    size_t assetCount;
    const DogfoodEntity* entities;
    size_t entityCount;
};

static const DogfoodAsset kDf03Assets[] = {
    { "df03aabbccddeeff0011223344556677", "assets/scripts/dogfood03_game.lua", "Script" },
    // 三个不同 GUID 指向同一张 test.png：df03 的脚本按 GUID 分别取三张贴图，
    // 用来验证「多张贴图可共存」。LoadManifest 直接写 m_ByGuid/m_ByPath，
    // 不走 RegisterExplicit 的重复检测，所以同路径多 GUID 是允许的。
    { "df03c3d4e5f6a7b8c9d0e1f2a3b4c5d6", "assets/textures/test.png", "Texture" },
    { "df03e5f6a7b8c9d0e1f2a3b4c5d6e7f8", "assets/textures/test.png", "Texture" },
    { "df03a9b8c7d6e5f4a3b2c1d0e9f8a7b6", "assets/textures/test.png", "Texture" },
};

// 实体表转录自 provisioned 工作区的 fixtures，理由同 kGp01Entities：
// 坐标是 df03_game.lua 追逐/拾取/边界判定所依赖的玩法几何。
static const DogfoodEntity kDf03Entities[] = {
    { "Player",      0.0f, 0.0f,  0.0f, -1, 0 },
    { "Chaser1",    -4.0f, 0.0f, -4.0f,  1, -1 },
    { "Chaser2",     4.0f, 0.0f, -4.0f,  1, -1 },
    { "Pickup_A",   -3.0f, 0.0f, -2.0f,  2, -1 },
    { "Pickup_B",    3.0f, 0.0f, -2.0f,  2, -1 },
    { "Pickup_C",    0.0f, 0.0f, -4.0f,  2, -1 },
    { "Pickup_D",   -3.5f, 0.0f, -6.0f,  2, -1 },
    { "Pickup_E",    3.5f, 0.0f, -6.0f,  2, -1 },
    { "Wall_Left",  -6.0f, 0.0f, -3.0f,  3, -1 },
    { "Wall_Right",  6.0f, 0.0f, -3.0f,  3, -1 },
    { "Wall_Top",    0.0f, 0.0f, -8.0f,  3, -1 },
    { "GoalZone",    0.0f, 0.0f, -9.5f,  3, -1 },
    { "SpawnPad",    0.0f, 0.0f,  2.0f,  3, -1 },
};

static const DogfoodAsset kDf05Assets[] = {
    { "df05aabbccddeeff0011223344556677", "assets/scripts/dogfood05_game.lua", "Script" },
};

static const DogfoodEntity kDf05Entities[] = {
    { "Player",        0.0f, 0.0f,   3.0f, -1, 0 },
    { "Enemy_Alpha",  -4.0f, 0.0f,  -4.0f, -1, -1 },
    { "Enemy_Beta",    4.0f, 0.0f,  -4.0f, -1, -1 },
    { "Enemy_Gamma",   0.0f, 0.0f,  -7.0f, -1, -1 },
    { "Enemy_Delta",  -5.0f, 0.0f,  -6.5f, -1, -1 },
    { "Enemy_Epsilon", 5.0f, 0.0f,  -6.5f, -1, -1 },
    { "Wall_North",    0.0f, 0.0f, -10.0f, -1, -1 },
    { "Wall_South",    0.0f, 0.0f,   8.0f, -1, -1 },
    { "Wall_West",    -8.0f, 0.0f,  -1.0f, -1, -1 },
    { "Wall_East",     8.0f, 0.0f,  -1.0f, -1, -1 },
    { "Pickup_Health",-5.0f, 0.0f,  -3.0f, -1, -1 },
    { "Pickup_Speed",  5.0f, 0.0f,  -3.0f, -1, -1 },
    { "Pickup_Damage", 0.0f, 0.0f,  -6.0f, -1, -1 },
};

static const DogfoodAsset kDf06Assets[] = {
    { "df06aabbccddeeff0011223344556677", "assets/scripts/dogfood06_game.lua", "Script" },
};

static const DogfoodEntity kDf06Entities[] = {
    { "Player",        0.0f, 0.0f,   3.0f, -1, 0 },
    { "Enemy_Grunt1", -2.0f, 0.0f,  -2.0f, -1, -1 },
    { "Enemy_Grunt2",  2.0f, 0.0f,  -2.0f, -1, -1 },
    { "Enemy_Ranged",  0.0f, 0.0f,  -6.0f, -1, -1 },
    { "Enemy_Tank",   -4.0f, 0.0f,  -5.0f, -1, -1 },
    { "Enemy_Scout",   4.0f, 0.0f,  -5.0f, -1, -1 },
    { "Enemy_Boss",    0.0f, 0.0f,  -9.0f, -1, -1 },
    { "Enemy_Minion1",-1.0f, 0.0f,  -7.0f, -1, -1 },
    { "Enemy_Minion2", 1.0f, 0.0f,  -7.0f, -1, -1 },
    { "Pickup_HP",    -5.0f, 0.0f,  -1.0f, -1, -1 },
    { "Pickup_DMG",    5.0f, 0.0f,  -1.0f, -1, -1 },
    { "Pickup_SPD",   -5.0f, 0.0f,  -6.0f, -1, -1 },
    { "Wall_N",        0.0f, 0.0f, -11.0f, -1, -1 },
    { "Wall_S",        0.0f, 0.0f,   5.0f, -1, -1 },
    { "GoalZone",      0.0f, 0.0f, -10.5f, -1, -1 },
};

static const DogfoodAsset kDf07Assets[] = {
    { "df07aabbccddeeff0011223344556677", "assets/scripts/dogfood07_game.lua", "Script" },
};

static const DogfoodEntity kDf07Entities[] = {
    { "Player",    0.0f, 0.0f,  4.0f, -1, 0 },
    { "Turret_A", -3.0f, 0.0f, -1.0f, -1, -1 },
    { "Turret_B",  0.0f, 0.0f, -3.0f, -1, -1 },
    { "Turret_C",  3.0f, 0.0f, -1.0f, -1, -1 },
    { "Gem_1",    -4.0f, 0.0f, -5.0f, -1, -1 },
    { "Gem_2",    -2.0f, 0.0f, -6.0f, -1, -1 },
    { "Gem_3",     0.0f, 0.0f, -7.0f, -1, -1 },
    { "Gem_4",     2.0f, 0.0f, -6.0f, -1, -1 },
    { "Gem_5",     4.0f, 0.0f, -5.0f, -1, -1 },
    { "Base_Core", 0.0f, 0.0f,  0.0f, -1, -1 },
};

static const DogfoodSpec kDogfoodSpecs[] = {
    { "dogfood03", kDf03Assets, std::size(kDf03Assets), kDf03Entities, std::size(kDf03Entities) },
    { "dogfood05", kDf05Assets, std::size(kDf05Assets), kDf05Entities, std::size(kDf05Entities) },
    { "dogfood06", kDf06Assets, std::size(kDf06Assets), kDf06Entities, std::size(kDf06Entities) },
    { "dogfood07", kDf07Assets, std::size(kDf07Assets), kDf07Entities, std::size(kDf07Entities) },
};

/// 清单没有任何引擎侧写入 API（LoadManifest 只读），所以这里直接发 JSON。
static bool WriteDogfoodManifest(const DogfoodSpec& spec, const std::string& dir) {
    nlohmann::json j;
    j["version"] = 1;
    j["assets"] = nlohmann::json::array();
    for (size_t i = 0; i < spec.assetCount; ++i) {
        const DogfoodAsset& a = spec.assets[i];
        Engine::ResourceGUID g = Engine::ResourceGUID::FromHex(a.guid);
        if (g.IsNull()) {
            std::fprintf(stderr, "[Dogfood] %s: unparseable guid %s\n", spec.id, a.guid);
            return false;
        }
        j["assets"].push_back({ { "guid", a.guid }, { "path", a.path }, { "type", a.type } });
    }

    const std::string path = dir + "/" + spec.id + ".manifest.json";
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::fprintf(stderr, "[Dogfood] cannot write %s\n", path.c_str());
        return false;
    }
    out << j.dump(2) << "\n";
    return true;
}

static bool GenerateDogfoodScenes(const std::string& outputDir) {
    fs::create_directories(outputDir);

    for (const auto& spec : kDogfoodSpecs) {
        Engine::Content::SceneSnapshot snap;
        snap.version = 1;

        for (size_t i = 0; i < spec.entityCount; ++i) {
            const DogfoodEntity& e = spec.entities[i];
            Engine::Content::SerializedEntity se;
            se.name = e.name;
            se.px = e.x;
            se.py = e.y;
            se.pz = e.z;

            auto bind = [&](int idx, Engine::ResourceGUID& guid, const char* what) -> bool {
                if (idx < 0) return true;
                if (static_cast<size_t>(idx) >= spec.assetCount) {
                    std::fprintf(stderr, "[Dogfood] %s/%s: %s asset index %d out of range\n",
                                 spec.id, e.name, what, idx);
                    return false;
                }
                guid = Engine::ResourceGUID::FromHex(spec.assets[idx].guid);
                if (guid.IsNull()) {
                    std::fprintf(stderr, "[Dogfood] %s/%s: bad guid for asset %d\n",
                                 spec.id, e.name, idx);
                    return false;
                }
                return true;
            };

            if (!bind(e.spriteAsset, se.spriteGuid, "sprite")) return false;
            if (!bind(e.scriptAsset, se.scriptGuid, "script")) return false;
            snap.entities.push_back(std::move(se));
        }

        const std::string scenePath = outputDir + "/" + spec.id + ".scene";
        if (!Engine::Content::SaveSnapshotToFile(snap, scenePath)) {
            std::fprintf(stderr, "[Dogfood] SaveSnapshotToFile failed: %s\n", scenePath.c_str());
            return false;
        }
        if (!WriteDogfoodManifest(spec, outputDir)) return false;

        std::printf("[Dogfood] %s: %zu entities, %zu asset(s)\n",
                    spec.id, snap.entities.size(), spec.assetCount);
    }
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
        "  --dogfood-scenes <dir>\n"
        "                Regenerate the dogfood03/05/06/07 scene + manifest pairs.\n"
        "                Their Lua scripts ARE tracked; only the scenes and the\n"
        "                *.manifest.json files are missing from a clean checkout.\n"
        "  --help        Show this help\n"
    );
}

int main(int argc, char* argv[]) {
    // 各模式参数个数不同（烘焙要 2 个路径，dogfood 只要 1 个目录），因此在这里
    // 只要求「模式 + 至少一个参数」，再由各分支自己校验所需个数。
    if (argc < 3) {
        PrintUsage();
        return 1;
    }

    std::string type = argv[1];
    std::string inputDir = argv[2];
    std::string outputDir = (argc > 3) ? argv[3] : std::string();

    bool result = false;

    if (type == "--shaders" || type == "--materials") {
        if (argc < 4) {
            std::fprintf(stderr, "%s requires <input_dir> <output_dir>\n", type.c_str());
            return 1;
        }
        if (type == "--shaders") {
            ShaderBaker baker;
            result = baker.ProcessDirectory(inputDir, outputDir);
        } else {
            MaterialBaker baker;
            result = baker.ProcessDirectory(inputDir, outputDir);
        }
    } else if (type == "--gp01-scene") {
        if (argc < 4) {
            std::fprintf(stderr, "--gp01-scene requires <manifest.json> <output.scene>\n");
            return 1;
        }
        result = GenerateGp01Scene(inputDir, outputDir);
    } else if (type == "--dogfood-scenes") {
        result = GenerateDogfoodScenes(inputDir);
    } else {
        std::fprintf(stderr, "Unknown type: %s\n", type.c_str());
        PrintUsage();
        return 1;
    }

    return result ? 0 : 1;
}