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
// 入口
// ════════════════════════════════════════════════════════════

static void PrintUsage() {
    std::printf(
        "Usage: AssetBakery --type <shaders|materials> <input_dir> <output_dir>\n"
        "\n"
        "Options:\n"
        "  --shaders    Bake GLSL shaders to .shader_bin format\n"
        "  --materials  Bake material templates to .mat_bin format\n"
        "  --help       Show this help\n"
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
    } else {
        std::fprintf(stderr, "Unknown type: %s\n", type.c_str());
        PrintUsage();
        return 1;
    }

    return result ? 0 : 1;
}