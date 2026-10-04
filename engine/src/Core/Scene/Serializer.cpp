#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace {
    Engine::Logger s_Log("JsonSerializer");
}

namespace Engine {

    // ── 函数级静态工厂映射（避免静态初始化顺序问题） ──
    std::unordered_map<std::string, JsonSerializer::ComponentFactory>& JsonSerializer::GetFactories() {
        static std::unordered_map<std::string, ComponentFactory> s_Factories;
        return s_Factories;
    }

    // ============================================================
    // JsonSerializer — 公共接口
    // ============================================================

    bool JsonSerializer::SaveToFile(const Scene& scene, const std::string& filePath) {
        nlohmann::json root = Serialize(scene);

        std::ofstream file(filePath);
        if (!file.is_open()) {
            s_Log.Error("Failed to write: {}", filePath);
            return false;
        }
        file << root.dump(4); 
        file.close();

        s_Log.Info("Scene saved: {}", filePath);
        return true;
    }

    bool JsonSerializer::LoadFromFile(Scene& scene, const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            s_Log.Error("Failed to read: {}", filePath);
            return false;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (const nlohmann::json::parse_error& e) {
            s_Log.Error("Parse error: {}", e.what());
            return false;
        } catch (const nlohmann::json::type_error& e) {
            // 语法合法但标量类型不符：与 parse_error 同等对待，返回失败
            s_Log.Error("Type error: {}", e.what());
            return false;
        }
        file.close();

        bool result = Deserialize(scene, root);
        if (result) {
            s_Log.Info("Scene loaded: {}", filePath);
        }
        return result;
    }

    // ============================================================
    // JsonSerializer — 序列化
    // ============================================================

    nlohmann::json JsonSerializer::Serialize(const Scene& scene) {
        nlohmann::json root;
        root["scene"]["name"] = scene.GetName();
        root["scene"]["properties"] = SerializeProperties(scene.GetProperties());

        auto& objects = root["scene"]["objects"];
        objects = nlohmann::json::array();
        for (const auto& obj : scene.GetObjects()) {
            objects.push_back(SerializeGameObject(*obj));
        }

        return root;
    }

    nlohmann::json JsonSerializer::SerializeProperties(const SceneProperties& props) {
        nlohmann::json j;
        j["ambientColor"] = { props.ambientColor.x, props.ambientColor.y,
                              props.ambientColor.z, props.ambientColor.w };
        j["gravity"] = { props.gravity.x, props.gravity.y };
        j["fogDensity"] = props.fogDensity;
        j["fogColor"] = { props.fogColor.x, props.fogColor.y,
                          props.fogColor.z, props.fogColor.w };
        j["enableFog"] = props.enableFog;
        j["renderingOrder"] = props.renderingOrder;
        return j;
    }

    nlohmann::json JsonSerializer::SerializeGameObject(const GameObject& obj) {
        nlohmann::json j;
        j["name"] = obj.GetName();
        j["active"] = obj.IsActive();

        // Transform（内建组件）
        j["transform"] = SerializeTransform(obj.GetTransform());

        // 组件数组 — 通过 ForEachComponent 迭代
        j["components"] = nlohmann::json::array();
        obj.ForEachComponent([&](const Component& comp) {
            nlohmann::json compJson;
            // 调用组件的虚 Serialize，写入 type + data
            comp.Serialize(compJson);
            if (compJson.contains("type")) {
                j["components"].push_back(std::move(compJson));
            }
        });

        // 子对象递归
        if (!obj.GetChildren().empty()) {
            j["children"] = nlohmann::json::array();
            for (const auto& child : obj.GetChildren()) {
                j["children"].push_back(SerializeGameObject(*child));
            }
        }

        return j;
    }

    nlohmann::json JsonSerializer::SerializeTransform(const TransformComponent& comp) {
        nlohmann::json j;
        const Vec3& p = comp.GetPosition();
        const Vec3& r = comp.GetRotation();
        const Vec3& s = comp.GetScale();
        j["position"] = { p.x, p.y, p.z };
        j["rotation"] = { r.x, r.y, r.z };
        j["scale"]    = { s.x, s.y, s.z };
        return j;
    }

    // ============================================================
    // JsonSerializer — 反序列化
    // ============================================================

    bool JsonSerializer::Deserialize(Scene& scene, const nlohmann::json& json) {
        if (!json.contains("scene")) {
            s_Log.Error("Missing 'scene' root");
            return false;
        }

        const auto& sceneJson = json["scene"];

        // 场景名称
        if (sceneJson.contains("name")) {
            if (!sceneJson["name"].is_string()) {
                s_Log.Error("Scene 'name' must be a string");
                return false;
            }
            scene.SetName(sceneJson["name"].get<std::string>());
        }

        // 场景属性
        if (sceneJson.contains("properties")) {
            if (!DeserializeProperties(scene.GetProperties(), sceneJson["properties"])) {
                s_Log.Error("Invalid scene property type");
                return false;
            }
        }

        // 根对象
        if (sceneJson.contains("objects") && sceneJson["objects"].is_array()) {
            for (const auto& objJson : sceneJson["objects"]) {
                auto obj = std::make_shared<GameObject>();
                if (!DeserializeGameObject(*obj, objJson)) {
                    s_Log.Error("Invalid object entry");
                    return false;
                }
                scene.AddObject(std::move(obj));
            }
        }

        return true;
    }

    bool JsonSerializer::DeserializeProperties(SceneProperties& props, const nlohmann::json& json) {
        if (json.contains("ambientColor") && json["ambientColor"].is_array() && json["ambientColor"].size() >= 4)
            props.ambientColor = Vec4(json["ambientColor"][0], json["ambientColor"][1],
                                      json["ambientColor"][2], json["ambientColor"][3]);
        if (json.contains("gravity") && json["gravity"].is_array() && json["gravity"].size() >= 2)
            props.gravity = Vec2(json["gravity"][0], json["gravity"][1]);
        // 标量字段：类型不符视为坏输入，直接返回 false。
        // 刻意**不**静默保留旧值/默认值 —— 那会把坏输入伪装成一次成功加载。
        if (json.contains("fogDensity")) {
            if (!json["fogDensity"].is_number()) return false;
            props.fogDensity = json["fogDensity"].get<float32>();
        }
        if (json.contains("fogColor") && json["fogColor"].is_array() && json["fogColor"].size() >= 4)
            props.fogColor = Vec4(json["fogColor"][0], json["fogColor"][1],
                                  json["fogColor"][2], json["fogColor"][3]);
        if (json.contains("enableFog")) {
            if (!json["enableFog"].is_boolean()) return false;
            props.enableFog = json["enableFog"].get<bool>();
        }
        if (json.contains("renderingOrder")) {
            if (!json["renderingOrder"].is_number()) return false;
            props.renderingOrder = json["renderingOrder"].get<uint32>();
        }
        return true;
    }

    bool JsonSerializer::DeserializeGameObject(GameObject& obj, const nlohmann::json& json) {
        // 名称
        if (json.contains("name")) {
            if (!json["name"].is_string()) return false;
            obj.SetName(json["name"].get<std::string>());
        }

        // 活跃状态
        if (json.contains("active")) {
            if (!json["active"].is_boolean()) return false;
            obj.SetActive(json["active"].get<bool>());
        }

        // Transform
        if (json.contains("transform")) {
            DeserializeTransform(obj.GetTransform(), json["transform"]);
        }

        // 组件
        if (json.contains("components") && json["components"].is_array()) {
            for (const auto& compJson : json["components"]) {
                DeserializeComponentForObject(obj, compJson);
            }
        }

        // 子对象递归
        if (json.contains("children") && json["children"].is_array()) {
            for (const auto& childJson : json["children"]) {
                auto child = std::make_shared<GameObject>();
                if (!DeserializeGameObject(*child, childJson)) return false;
                obj.AddChild(std::move(child));
            }
        }

        return true;
    }

    bool JsonSerializer::DeserializeTransform(TransformComponent& comp, const nlohmann::json& json) {
        if (json.contains("position") && json["position"].is_array() && json["position"].size() >= 3)
            comp.SetPosition(json["position"][0], json["position"][1], json["position"][2]);
        if (json.contains("rotation") && json["rotation"].is_array() && json["rotation"].size() >= 3)
            comp.SetRotation(json["rotation"][0], json["rotation"][1], json["rotation"][2]);
        if (json.contains("scale") && json["scale"].is_array() && json["scale"].size() >= 3)
            comp.SetScale(Vec3(json["scale"][0], json["scale"][1], json["scale"][2]));
        return true;
    }

    bool JsonSerializer::DeserializeComponentForObject(GameObject& obj, const nlohmann::json& json) {
        if (!json.contains("type") || !json["type"].is_string())
            return false;

        const std::string typeName = json["type"];
        auto& factories = GetFactories();
        auto it = factories.find(typeName);
        if (it != factories.end()) {
            Component* comp = it->second(obj);
            if (comp) return comp->Deserialize(json);
        }

        s_Log.Error("Unknown component type: {}", typeName);
        return false;
    }

} // namespace Engine