#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // SceneGroupConfig 实现
    // ═══════════════════════════════════════════════════════════════

    SceneGroupConfig SceneGroupConfig::LoadFromJson(const std::string& jsonContent) {
        SceneGroupConfig config;

        try {
            nlohmann::json root = nlohmann::json::parse(jsonContent);

            // 兼容两种格式：{ "groups": [...] } 或直接数组 [...]
            nlohmann::json groupsArray;
            if (root.is_array()) {
                groupsArray = root;
            } else if (root.contains("groups")) {
                groupsArray = root["groups"];
            } else {
                ENGINE_LOG_ERROR("SceneGroupConfig", "Invalid JSON format: expected 'groups' array or root array");
                return config;
            }

            for (const auto& groupJson : groupsArray) {
                SceneGroup group;
                group.groupName = groupJson.value("groupName", "UnnamedGroup");
                group.masterScene = groupJson.value("masterScene", "");
                group.description = groupJson.value("description", "");
                group.boundingRadius = groupJson.value("boundingRadius", 0.0f);
                group.isStreaming = groupJson.value("isStreaming", false);

                // 解析子场景列表
                if (groupJson.contains("subScenes") && groupJson["subScenes"].is_array()) {
                    for (const auto& subJson : groupJson["subScenes"]) {
                        SceneGroupEntry entry;
                        entry.sceneName   = subJson.value("sceneName", "");
                        entry.required    = subJson.value("required", true);
                        entry.loadAsync   = subJson.value("loadAsync", true);
                        entry.loadPriority = subJson.value("loadPriority", 0);
                        group.subScenes.push_back(std::move(entry));
                    }
                }

                config.groups.push_back(std::move(group));
            }

            ENGINE_LOG_INFO("SceneGroupConfig", "Parsed {} scene groups from JSON", config.groups.size());

        } catch (const nlohmann::json::parse_error& e) {
            ENGINE_LOG_ERROR("SceneGroupConfig", "JSON parse error: {}", e.what());
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("SceneGroupConfig", "Unexpected error: {}", e.what());
        }

        return config;
    }

    SceneGroupConfig SceneGroupConfig::LoadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            ENGINE_LOG_ERROR("SceneGroupConfig", "Cannot open file: {}", filePath);
            return SceneGroupConfig{};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return LoadFromJson(buffer.str());
    }

    std::string SceneGroupConfig::ToJson() const {
        nlohmann::json root;
        nlohmann::json groupsArray = nlohmann::json::array();

        for (const auto& group : groups) {
            nlohmann::json groupJson;
            groupJson["groupName"]      = group.groupName;
            groupJson["masterScene"]    = group.masterScene;
            groupJson["description"]    = group.description;
            groupJson["boundingRadius"] = group.boundingRadius;
            groupJson["isStreaming"]    = group.isStreaming;

            nlohmann::json subArray = nlohmann::json::array();
            for (const auto& entry : group.subScenes) {
                nlohmann::json entryJson;
                entryJson["sceneName"]    = entry.sceneName;
                entryJson["required"]     = entry.required;
                entryJson["loadAsync"]    = entry.loadAsync;
                entryJson["loadPriority"] = entry.loadPriority;
                subArray.push_back(std::move(entryJson));
            }
            groupJson["subScenes"] = std::move(subArray);

            groupsArray.push_back(std::move(groupJson));
        }

        root["groups"] = std::move(groupsArray);
        return root.dump(4);
    }

    const SceneGroup* SceneGroupConfig::FindGroup(const std::string& groupName) const {
        for (const auto& group : groups) {
            if (group.groupName == groupName) {
                return &group;
            }
        }
        return nullptr;
    }

    std::vector<const SceneGroup*> SceneGroupConfig::FindGroupsForScene(
        const std::string& sceneName) const
    {
        std::vector<const SceneGroup*> result;
        for (const auto& group : groups) {
            // 检查 masterScene 或 subScenes 中是否包含该场景
            if (group.masterScene == sceneName) {
                result.push_back(&group);
                continue;
            }
            for (const auto& entry : group.subScenes) {
                if (entry.sceneName == sceneName) {
                    result.push_back(&group);
                    break;
                }
            }
        }
        return result;
    }

} // namespace Engine