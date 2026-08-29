/**
 * @file SceneSerializerV1.cpp
 * @brief 场景序列化 v1 实现（Ring10-12）
 */

#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Log.h"

#include <fstream>

namespace Engine::Content {

namespace {
    Logger s_Log("SceneSerializerV1");
    constexpr int kFormatVersion = 1;
}

// ── 快照 ↔ JSON ─────────────────────────────────────────

nlohmann::json SerializeSnapshot(const SceneSnapshot& snap) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["version"] = snap.version;
    j["entities"] = nlohmann::json::array();
    for (const auto& e : snap.entities) {
        nlohmann::json je = {
            { "name", e.name },
            { "position", { e.px, e.py, e.pz } },
        };
        if (!e.spriteGuid.IsNull()) je["sprite"] = e.spriteGuid.ToHex();
        if (!e.scriptGuid.IsNull()) je["script"] = e.scriptGuid.ToHex();
        // 契约组件（F1-D）：type = 稳定类型名，data = 组件 Serialize 产出
        if (!e.components.empty()) {
            auto cj = nlohmann::json::array();
            for (const auto& c : e.components)
                cj.push_back({ { "type", c.type }, { "data", c.data } });
            je["components"] = std::move(cj);
        }
        j["entities"].push_back(je);
    }
    return j;
}

bool DeserializeSnapshot(const nlohmann::json& j, SceneSnapshot& out,
                         std::string& err) {
    out = {};
    if (!j.is_object()) { err = "root is not an object"; return false; }
    if (!j.contains("format") || j["format"] != "engine.scene") {
        err = "not an engine.scene file"; return false;
    }
    if (j.contains("version")) {
        const int v = j["version"].get<int>();
        if (v > kFormatVersion) {
            err = "unsupported future version " + std::to_string(v);
            return false;
        }
    }
    if (!j.contains("entities") || !j["entities"].is_array()) {
        err = "missing entities array"; return false;
    }

    for (const auto& e : j["entities"]) {
        SerializedEntity se;
        if (!e.contains("name") || !e["name"].is_string()) {
            continue;   // 契约：缺 name 的实体跳过（调用方经 warnings 得知需自行统计）
        }
        se.name = e["name"].get<std::string>();
        if (e.contains("position") && e["position"].is_array()
                && e["position"].size() >= 3) {
            se.px = e["position"][0].get<float>();
            se.py = e["position"][1].get<float>();
            se.pz = e["position"][2].get<float>();
        }
        if (e.contains("sprite") && e["sprite"].is_string())
            se.spriteGuid = ResourceGUID::FromHex(e["sprite"].get<std::string>());
        if (e.contains("script") && e["script"].is_string())
            se.scriptGuid = ResourceGUID::FromHex(e["script"].get<std::string>());
        // 契约组件（F1-D）：未知类型保留数据但不在加载层扩展（负路径在
        // InstantiateScene 处理），这里原样还原到快照避免信息丢失。
        if (e.contains("components") && e["components"].is_array()) {
            for (const auto& c : e["components"]) {
                if (!c.is_object() || !c.contains("type") || !c["type"].is_string())
                    continue;   // 结构损坏的子项跳过（整体仍然可加载）
                SerializedComponent sc;
                sc.type = c["type"].get<std::string>();
                if (c.contains("data") && c["data"].is_object())
                    sc.data = c["data"];
                se.components.push_back(std::move(sc));
            }
        }
        out.entities.push_back(std::move(se));
    }
    return true;
}

// ── Scene 桥接 ──────────────────────────────────────────

SceneSnapshot CaptureScene(const Scene& scene,
                           const std::vector<EntityContentBinding>& bindings) {
    SceneSnapshot snap;
    const auto& objs = scene.GetObjects();
    snap.entities.reserve(objs.size());
    for (size_t i = 0; i < objs.size(); ++i) {
        const auto& obj = *objs[i];
        SerializedEntity e;
        e.name = obj.GetName();
        const Vec3 p = obj.GetTransform().GetPosition();
        e.px = p.x; e.py = p.y; e.pz = p.z;
        if (i < bindings.size()) {
            e.spriteGuid = bindings[i].spriteGuid;
            e.scriptGuid = bindings[i].scriptGuid;
        }
        // 契约组件：只收编有稳定类型名的组件（GetComponentTypeName() != nullptr），
        // 避免把 Sprite 等仍走顶层 GUID 的既有组件双重序列化。
        obj.ForEachComponent([&e](const Component& c) {
            const char* tn = c.GetComponentTypeName();
            if (!tn) return;
            SerializedComponent sc;
            sc.type = tn;
            c.Serialize(sc.data);
            e.components.push_back(std::move(sc));
        });
        snap.entities.push_back(std::move(e));
    }
    return snap;
}

LoadResult InstantiateScene(const SceneSnapshot& snap, Scene& outScene,
                            TextureManager& texMgr,
                            const ContentRegistry& registry) {
    LoadResult r;
    r.ok = true;

    for (const auto& e : snap.entities) {
        auto obj = std::make_shared<::Engine::GameObject>(e.name);
        obj->GetTransform().SetPosition(e.px, e.py, e.pz);

        EntityContentBinding binding;   // 默认 Null

        // Sprite：GUID 可解析 → 挂组件；缺失 → warning + 无 Sprite（契约）
        if (!e.spriteGuid.IsNull()) {
            const std::string path = registry.ResolvePath(e.spriteGuid);
            if (path.empty()) {
                r.warnings.push_back("entity '" + e.name
                    + "': sprite GUID missing in registry (loaded without sprite)");
            } else {
                auto* spr = obj->AddComponent<SpriteComponent>();
                spr->SetTexture(texMgr, path);
                binding.spriteGuid = e.spriteGuid;
            }
        }

        // Script：同契约 —— 缺失非致命，实体照常存在
        if (!e.scriptGuid.IsNull()) {
            const std::string path = registry.ResolvePath(e.scriptGuid);
            if (path.empty()) {
                r.warnings.push_back("entity '" + e.name
                    + "': script GUID missing in registry (loaded without script)");
            } else {
                binding.scriptGuid = e.scriptGuid;
            }
        }

        // 契约组件（F1-D）：未知类型 → warning（实体照常加载）；已知 → 反序列化 + 挂载
        for (const auto& sc : e.components) {
            if (!ComponentRegistryGo::IsRegistered(sc.type)) {
                r.warnings.push_back("entity '" + e.name
                    + "': unknown contract component type '" + sc.type
                    + "' (skipped)");
                continue;
            }
            auto comp = ComponentRegistryGo::Create(sc.type);
            if (!comp) continue;
            if (!comp->Deserialize(sc.data)) {
                r.warnings.push_back("entity '" + e.name
                    + "': component '" + sc.type + "' DataBad (kept defaults)");
            }
            obj->Attach(comp);
        }

        outScene.AddObject(obj);
        r.bindings.push_back(binding);
    }
    return r;
}

// ── 文件 IO ─────────────────────────────────────────────

bool SaveSnapshotToFile(const SceneSnapshot& snap, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f << SerializeSnapshot(snap).dump(2);
    return f.good();
}

bool LoadSnapshotFromFile(const std::string& path, SceneSnapshot& out,
                          std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { err = "cannot open file: " + path; return false; }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& ex) {
        err = std::string("corrupted scene file: ") + ex.what();
        return false;                       // fail-clean：out 保持空
    }
    return DeserializeSnapshot(j, out, err);
}

} // namespace Engine::Content
