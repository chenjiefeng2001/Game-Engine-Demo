#pragma once

/**
 * @file SceneSerializerV1.h
 * @brief 场景序列化 v1 — Content Pipeline 垂直切片（Ring10-12）
 *
 * 序列化模型（v1 范围红线）：
 *   Entity = { Name, Position(x,y,z), SpriteAssetGUID?, ScriptAssetGUID? }
 *   —— 只存 ID/GUID，永不序列化运行时指针（与 Scripting 句柄原则一致）
 *
 * 加载失败契约（Phase E 显式化，实现不得自行其是）：
 *   | 情形                     | 契约                                   |
 *   |--------------------------|----------------------------------------|
 *   | JSON 解析失败/结构损坏   | Load 失败，返回 err，不产生部分状态      |
 *   | 实体缺 name              | 该实体跳过 + 记入 warnings             |
 *   | Texture GUID 缺失        | 实体照常加载（无 Sprite），记 warning    |
 *   | Script GUID 缺失         | 实体照常加载（无脚本绑定），记 warning  |
 *   | Registry 无法解析路径    | 同"缺失"处理                           |
 */

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Engine {
    class Scene;
    class TextureManager;
}

namespace Engine::Content {

    // ── 快照模型（格式稳定层：与运行时对象解耦）──
    /// 一个已收编的 Contract 组件实例（F1-D，组件化快照）
    /// type = GetComponentTypeName() 稳定字符串；data = Component::Serialize 产出。
    struct SerializedComponent {
        std::string   type;
        nlohmann::json data = nlohmann::json::object();
    };

    struct SerializedEntity {
        std::string  name;
        float        px = 0.f, py = 0.f, pz = 0.f;
        ResourceGUID spriteGuid;   // Null = 无
        ResourceGUID scriptGuid;   // Null = 无
        /// 已收编的 Contract 组件（F1-D，组件化快照）。
        /// 默认空 = 无契约组件；sprite/script 顶层字段保留向后兼容。
        std::vector<SerializedComponent> components;
    };

    struct SceneSnapshot {
        int32_t version = 1;
        std::vector<SerializedEntity> entities;
    };

    // ── 快照 ↔ JSON ──
    nlohmann::json SerializeSnapshot(const SceneSnapshot& snap);
    /// 结构损坏 → false + err；成功 → true（实体级问题走 warnings）
    bool DeserializeSnapshot(const nlohmann::json& j, SceneSnapshot& out,
                             std::string& err);

    // ── Scene 桥接 ──
    /// 从真实 Scene 抽取快照；sprite/script GUID 来自 bindings（按实体名键控）
    struct EntityContentBinding {
        ResourceGUID spriteGuid;
        ResourceGUID scriptGuid;
    };
    SceneSnapshot CaptureScene(
        const Scene& scene,
        const std::vector<EntityContentBinding>& bindings);   // 顺序与 scene 对象序一致

    /// 将快照实例化为真实 GameObjects：
    ///   sprite GUID 可解析 → AddComponent<SpriteComponent>(texMgr, path)
    /// 返回每个实体的内容绑定（供后续保存/脚本生成）
    struct LoadResult {
        bool ok = false;
        std::vector<std::string> warnings;
        std::vector<EntityContentBinding> bindings;   // 与加载出的对象一一对应
    };
    LoadResult InstantiateScene(const SceneSnapshot& snap, Scene& outScene,
                                class TextureManager& texMgr,
                                const ContentRegistry& registry);

    // ── 文件 IO ──
    bool SaveSnapshotToFile(const SceneSnapshot& snap, const std::string& path);
    bool LoadSnapshotFromFile(const std::string& path, SceneSnapshot& out,
                              std::string& err);

} // namespace Engine::Content
