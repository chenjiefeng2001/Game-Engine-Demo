#pragma once

/**
 * @file Serializer.h
 * @brief 场景序列化/反序列化 — 将 Scene 导出为 JSON 格式，支持重建
 *
 * 使用 nlohmann/json 库，场景保存为 .json 文件。
 * 所有组件通过虚函数 Serialize/Deserialize 自动纳入序列化。
 *
 * 组件类型通过注册式工厂管理：每个组件的 .cpp 中调用
 * JsonSerializer::RegisterComponentType<T>("TypeName") 即可自动注册，
 * 无需修改 Serializer 代码。
 *
 * JSON 格式示例：
 * @code
 * {
 *     "scene": {
 *         "name" : "MainScene",
 *         "properties": {
 *             "ambientColor" : [0.02, 0.02, 0.03, 1.0],
 *             "gravity" : [0.0, -9.81],
 *             "fogDensity" : 0.0,
 *             "enableFog" : false,
 *             "renderingOrder" : 0
 *         },
 *         "objects": [{
 *             "name" : "Player",
 *             "active" : true,
 *             "transform" : { "position" : [0,0,0], "rotation" : [0,0,0], "scale" : [1,1,1] },
 *             "components" : [{
 *                 "type" : "SpriteComponent",
 *                 "data" : { "texture" : "tex.png", "color" : [1,1,1,1] }
 *             }],
 *             "children" : [...]
 *         }]
 *     }
 * }
 * @endcode
 *
 * 使用方法：
 * @code
 *   Scene scene("Main");
 *   // ... 构建场景 ...
 *   JsonSerializer::SaveToFile(scene, "assets/scenes/main.json");
 *
 *   Scene loaded;
 *   JsonSerializer::LoadFromFile(loaded, "assets/scenes/main.json");
 * @endcode
 */

#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Types.h"

#include <nlohmann/json.hpp>

#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <type_traits>

namespace Engine {

    // ============================================================
    // JSON 场景序列化器
    // ============================================================
    class JsonSerializer {
    public:
        /** 将 Scene 保存到 JSON 文件 */
        static bool SaveToFile(const Scene& scene, const std::string& filePath);

        /** 从 JSON 文件加载 Scene */
        static bool LoadFromFile(Scene& scene, const std::string& filePath);

        /** 将 Scene 序列化为 JSON 对象 */
        static nlohmann::json Serialize(const Scene& scene);

        /** 从 JSON 对象反序列化到 Scene */
        static bool Deserialize(Scene& scene, const nlohmann::json& json);

        // ── 注册式组件工厂 ──
        /**
         * @brief 注册组件类型到反序列化工厂
         * @tparam T 组件类型（必须继承自 Component）
         * @param typeName JSON 中 "type" 字段的值
         *
         * 在组件 .cpp 的匿名 namespace 中调用：
         * @code
         *   namespace {
         *       bool reg = []() {
         *           JsonSerializer::RegisterComponentType<SpriteComponent>("SpriteComponent");
         *           return true;
         *       }();
         *   }
         * @endcode
         */
        template<typename T>
        static void RegisterComponentType(const std::string& typeName) {
            static_assert(std::is_base_of_v<Component, T>,
                          "T must derive from Component");
            GetFactories()[typeName] = [](GameObject& obj) -> Component* {
                auto* comp = obj.GetComponent<T>();
                if (!comp) comp = obj.AddComponent<T>();
                return comp;
            };
        }

    private:
        using ComponentFactory = std::function<Component*(GameObject&)>;
        static std::unordered_map<std::string, ComponentFactory>& GetFactories();

        // ── 内部：保存 ──
        static nlohmann::json SerializeGameObject(const GameObject& obj);
        static nlohmann::json SerializeTransform(const TransformComponent& comp);
        static nlohmann::json SerializeProperties(const SceneProperties& props);

        // ── 内部：加载 ──
        static bool DeserializeGameObject(GameObject& obj, const nlohmann::json& json);
        static bool DeserializeTransform(TransformComponent& comp, const nlohmann::json& json);
        static bool DeserializeProperties(SceneProperties& props, const nlohmann::json& json);
        static bool DeserializeComponentForObject(GameObject& obj, const nlohmann::json& json);
    };

} // namespace Engine