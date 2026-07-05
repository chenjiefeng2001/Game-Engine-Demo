#pragma once

/**
 * @file AssetTypes.h
 * @brief 工业级资产类型定义 — GUID 系统、资产元数据、导入设置、依赖追踪
 *
 * 核心设计：
 *   - 每个资产生成唯一 GUID（UUID v4），通过 .meta 文件持久化
 *   - 文件路径移动不影响引用（引擎内部用 GUID 追踪）
 *   - 虚拟路径映射（VFS）将逻辑路径映射到物理路径
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <cstdint>
#include <array>
#include <random>

namespace Engine {

    // ============================================================
    // 1. GUID 系统（128 位 UUID v4）
    // ============================================================
    struct GUID {
        uint64_t high = 0;  ///< 高 64 位
        uint64_t low  = 0;  ///< 低 64 位

        bool operator==(const GUID& other) const noexcept {
            return high == other.high && low == other.low;
        }
        bool operator!=(const GUID& other) const noexcept {
            return !(*this == other);
        }
        bool operator<(const GUID& other) const noexcept {
            if (high != other.high) return high < other.high;
            return low < other.low;
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return high != 0 || low != 0;
        }

        [[nodiscard]] std::string ToString() const;
        static GUID FromString(const std::string& str);
        static GUID Generate();
    };

    // ============================================================
    // 2. 资产类型枚举
    // ============================================================
    enum class AssetType : uint8_t {
        Unknown,
        Texture,
        Shader,
        AudioClip,
        Font,
        Scene,
        Model,
        Material,
        Prefab,
        Script,
        Animation,
        AnimationClip,
        PhysicsMaterial,
        Video,
        ParticleSystem,
        RenderTexture,
        ComputeShader,
        Custom
    };

    inline const char* AssetTypeName(AssetType type) {
        switch (type) {
            case AssetType::Texture:     return "Texture";
            case AssetType::Shader:      return "Shader";
            case AssetType::AudioClip:   return "Audio Clip";
            case AssetType::Font:        return "Font";
            case AssetType::Scene:       return "Scene";
            case AssetType::Model:       return "Model";
            case AssetType::Material:    return "Material";
            case AssetType::Prefab:      return "Prefab";
            case AssetType::Script:      return "Script";
            case AssetType::Animation:   return "Animation";
            case AssetType::AnimationClip: return "Animation Clip";
            case AssetType::PhysicsMaterial: return "Physics Material";
            case AssetType::Video:       return "Video";
            case AssetType::ParticleSystem: return "Particle System";
            case AssetType::RenderTexture: return "Render Texture";
            case AssetType::ComputeShader: return "Compute Shader";
            default:                     return "Unknown";
        }
    }

    inline AssetType AssetTypeFromExtension(const std::string& ext) {
        auto lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
            lower == ".bmp" || lower == ".tga" || lower == ".hdr" ||
            lower == ".psd" || lower == ".dds") return AssetType::Texture;
        if (lower == ".glsl" || lower == ".vert" || lower == ".frag" ||
            lower == ".comp") return AssetType::Shader;
        if (lower == ".wav" || lower == ".mp3" || lower == ".ogg" ||
            lower == ".flac" || lower == ".aiff") return AssetType::AudioClip;
        if (lower == ".ttf" || lower == ".otf") return AssetType::Font;
        if (lower == ".scene" || lower == ".unity") return AssetType::Scene;
        if (lower == ".obj" || lower == ".fbx" || lower == ".gltf" ||
            lower == ".glb") return AssetType::Model;
        if (lower == ".mat") return AssetType::Material;
        if (lower == ".prefab") return AssetType::Prefab;
        if (lower == ".cs" || lower == ".cpp" || lower == ".h" ||
            lower == ".lua") return AssetType::Script;
        if (lower == ".anim") return AssetType::AnimationClip;
        return AssetType::Unknown;
    }

    // ============================================================
    // 3. 资产导入设置（每种类型有不同的配置）
    // ============================================================
    struct TextureImportSettings {
        enum class Format : uint8_t { RGBA32, RGB24, BC1, BC3, BC5, BC7, ASTC };
        enum class WrapMode : uint8_t { Repeat, Clamp, Mirror, MirrorOnce };
        enum class FilterMode : uint8_t { Point, Bilinear, Trilinear };
        enum class MipGen : uint8_t { None, Box, Lanczos, Kaiser };

        Format      format           = Format::BC7;
        WrapMode    wrapU            = WrapMode::Repeat;
        WrapMode    wrapV            = WrapMode::Repeat;
        FilterMode  filter           = FilterMode::Bilinear;
        MipGen      mipGen           = MipGen::Kaiser;
        int         maxResolution    = 4096;
        float       compressionQuality = 0.8f;
        bool        sRGB             = true;
        bool        generateMipMaps  = true;
        bool        isNormalMap      = false;
        bool        isSprite         = false;
        float       spritePixelsPerUnit = 100.0f;
    };

    struct ModelImportSettings {
        float       scaleFactor      = 1.0f;
        bool        importMaterials  = true;
        bool        importAnimations = true;
        bool        optimizeMeshes   = true;
        bool        generateColliders = false;
        float       meshCompression = 0.0f;
        int         maxBonesPerVertex = 4;
    };

    struct AudioImportSettings {
        enum class Compression : uint8_t { PCM, ADPCM, Vorbis, MP3 };
        Compression compression     = Compression::Vorbis;
        float       quality         = 0.8f;
        bool        streamFromDisk  = false;  ///< 大文件流式加载
        bool        normalize       = true;
        bool        loopable        = false;
    };

    // ============================================================
    // 4. 资产元数据（存储在 .meta 文件中）
    // ============================================================
    struct AssetMeta {
        GUID            guid;               ///< 唯一标识符
        AssetType       type    = AssetType::Unknown;
        std::string     originalPath;       ///< 原始导入路径
        std::string     virtualPath;        ///< 虚拟路径（可移动）
        int64_t         fileSize    = 0;
        int64_t         lastModifiedTime = 0;
        std::string     md5Hash;            ///< 文件内容哈希
        bool            isDirectory = false;

        // 导入时间戳
        int64_t         importedTime = 0;
        int64_t         sourceFileTime = 0;

        // 导入状态
        bool            importError = false;
        std::string     errorMessage;

        // 各类型导入设置（通过 union/变体存储）
        TextureImportSettings   textureSettings;
        ModelImportSettings     modelSettings;
        AudioImportSettings     audioSettings;
    };

    // ============================================================
    // 5. 依赖关系
    // ============================================================
    struct AssetDependency {
        GUID    assetGUID;
        std::string assetPath;    ///< 被依赖资产路径
        bool    isDirect = true;  ///< 直接依赖还是间接依赖
    };

    struct AssetDependencyInfo {
        GUID guid;
        std::vector<AssetDependency> dependsOn;      ///< 此资产依赖哪些资产
        std::vector<AssetDependency> usedBy;          ///< 哪些资产依赖此资产
        bool dirty = false;      ///< 需要重新计算依赖
    };

    // ============================================================
    // 6. 资产状态与版本控制标记
    // ============================================================
    enum class AssetVCStatus : uint8_t {
        None,               ///< 未追踪
        Synced,             ///< 已同步
        CheckedOut,         ///< 自己已签出（蓝色勾）
        LockedByOther,      ///< 他人锁定（红色叉）
        Added,              ///< 新增（加号）
        Modified,           ///< 已修改
        Deleted,            ///< 已删除
        Conflict,           ///< 冲突（感叹号）
        OutOfSync           ///< 版本落后
    };

    // ============================================================
    // 7. 资产浏览条目（UI 显示用）
    // ============================================================
    struct AssetBrowserEntry {
        GUID                guid;
        std::string         displayName;
        std::string         virtualPath;
        std::string         physicalPath;
        AssetType           type        = AssetType::Unknown;
        int64_t             fileSize    = 0;
        int64_t             lastModified = 0;
        bool                isDirectory = false;
        bool                isFavorite  = false;
        AssetVCStatus       vcStatus    = AssetVCStatus::None;
        bool                hasMetaFile = false;

        // 缩略图（纹理 ID 或路径）
        uint32_t            thumbnailTextureID = 0;
        bool                hasThumbnail = false;
    };

    // ============================================================
    // 8. 收藏集（虚拟文件夹）
    // ============================================================
    struct AssetCollection {
        std::string         name;
        std::string         description;
        std::vector<GUID>   assetGUIDs;     ///< 收藏集中的资产
        bool                isBuiltin = false;
    };

    // ============================================================
    // 9. 搜索过滤器
    // ============================================================
    struct AssetSearchFilter {
        std::string     nameFilter;
        AssetType       typeFilter       = AssetType::Unknown;  ///< Unknown = 全部
        bool            unusedOnly       = false;
        bool            recentOnly       = false;
        bool            favoritesOnly    = false;
        int64_t         minFileSize      = 0;
        int64_t         maxFileSize      = INT64_MAX;
        int             minResolution    = 0;        ///< 仅贴图
        int             maxResolution    = INT_MAX;  ///< 仅贴图
        AssetVCStatus   vcStatusFilter   = AssetVCStatus::None;

        // 时间范围
        int64_t         modifiedAfter    = 0;
        int64_t         modifiedBefore   = INT64_MAX;

        // 标签系统
        std::vector<std::string> tags;
    };

} // namespace Engine

// GUID 哈希支持（用于 unordered_map）
namespace std {
    template<> struct hash<Engine::GUID> {
        size_t operator()(const Engine::GUID& g) const noexcept {
            return static_cast<size_t>(g.high ^ (g.low * 0x9e3779b97f4a7c15ULL));
        }
    };
}