#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace Engine {

    // ═══════════════════════════════════════════════════════════
    // 构造/析构
    // ═══════════════════════════════════════════════════════════

    LevelManager::~LevelManager() {
        // 逆序卸载所有关卡
        m_ActiveLevel = nullptr;
        m_AdditiveLevels.clear();
        m_Levels.clear();
    }

    // ═══════════════════════════════════════════════════════════
    // 关卡注册表管理
    // ═══════════════════════════════════════════════════════════

    void LevelManager::RegisterLevel(const LevelInfo& info) {
        if (m_Levels.find(info.name) != m_Levels.end()) {
            ENGINE_LOG_WARN("LevelManager", "Level '{}' already registered, overwriting", info.name);
        }
        m_Levels[info.name] = std::make_unique<Level>(info);
        ENGINE_LOG_INFO("LevelManager", "Registered level: '{}' ({})", info.name, info.filePath);
    }

    void LevelManager::RegisterLevels(const std::vector<LevelInfo>& infos) {
        for (const auto& info : infos) {
            RegisterLevel(info);
        }
    }

    void LevelManager::UnregisterLevel(const std::string& name) {
        auto it = m_Levels.find(name);
        if (it == m_Levels.end()) {
            ENGINE_LOG_WARN("LevelManager", "Cannot unregister '{}': not found", name);
            return;
        }

        // 如果是活动关卡，先取消激活
        if (m_ActiveLevel == it->second.get()) {
            m_ActiveLevel = nullptr;
        }

        // 从叠加列表中移除
        auto addIt = std::find(m_AdditiveLevels.begin(), m_AdditiveLevels.end(), it->second.get());
        if (addIt != m_AdditiveLevels.end()) {
            m_AdditiveLevels.erase(addIt);
        }

        m_Levels.erase(it);
        ENGINE_LOG_INFO("LevelManager", "Unregistered level: '{}'", name);
    }

    Level* LevelManager::FindLevel(const std::string& name) {
        auto it = m_Levels.find(name);
        return (it != m_Levels.end()) ? it->second.get() : nullptr;
    }

    const Level* LevelManager::FindLevel(const std::string& name) const {
        auto it = m_Levels.find(name);
        return (it != m_Levels.end()) ? it->second.get() : nullptr;
    }

    std::vector<Level*> LevelManager::GetLevelList() {
        std::vector<Level*> result;
        result.reserve(m_Levels.size());
        for (auto& [_, level] : m_Levels) {
            result.push_back(level.get());
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════
    // 关卡加载/卸载（同步）
    // ═══════════════════════════════════════════════════════════

    bool LevelManager::LoadLevel(const std::string& name) {
        Level* level = FindLevel(name);
        if (!level) {
            ENGINE_LOG_ERROR("LevelManager", "Cannot load '{}': level not registered", name);
            return false;
        }

        // 如果已加载或正在加载，直接返回
        LevelState state = level->GetState();
        if (state == LevelState::Loaded || state == LevelState::Active) {
            return true;
        }

        // 先加载依赖关卡
        for (const auto& dep : level->GetInfo().dependencies) {
            if (!LoadLevel(dep)) {
                ENGINE_LOG_ERROR("LevelManager", "Failed to load dependency '{}' for '{}'", dep, name);
                return false;
            }
        }

        bool success = level->Load();
        if (success && m_OnLevelLoaded) {
            m_OnLevelLoaded(name);
        }
        return success;
    }

    void LevelManager::UnloadLevel(const std::string& name) {
        Level* level = FindLevel(name);
        if (!level) return;

        // 检查是否有其他关卡依赖此关卡
        for (auto& [n, l] : m_Levels) {
            if (n == name) continue;
            const auto& deps = l->GetInfo().dependencies;
            if (std::find(deps.begin(), deps.end(), name) != deps.end()) {
                ENGINE_LOG_WARN("LevelManager", "Cannot unload '{}': dependency of '{}'", name, n);
                return;
            }
        }

        if (m_ActiveLevel == level) {
            m_ActiveLevel = nullptr;
        }
        auto addIt = std::find(m_AdditiveLevels.begin(), m_AdditiveLevels.end(), level);
        if (addIt != m_AdditiveLevels.end()) {
            m_AdditiveLevels.erase(addIt);
        }

        level->Unload();
        if (m_OnLevelUnloaded) {
            m_OnLevelUnloaded(name);
        }
    }

    void LevelManager::UnloadAllNonPersistent(const std::string& exceptName) {
        std::vector<std::string> toUnload;
        for (auto& [name, level] : m_Levels) {
            if (name == exceptName) continue;
            if (level->GetInfo().isPersistent) continue;
            toUnload.push_back(name);
        }
        for (const auto& name : toUnload) {
            UnloadLevel(name);
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 关卡激活/切换
    // ═══════════════════════════════════════════════════════════

    bool LevelManager::SetActiveLevel(const std::string& name) {
        Level* level = FindLevel(name);
        if (!level) {
            ENGINE_LOG_ERROR("LevelManager", "Cannot set active '{}': not registered", name);
            return false;
        }

        // 加载如果尚未加载
        if (level->GetState() == LevelState::Unloaded) {
            if (!LoadLevel(name)) return false;
        }

        // 取消激活当前活动关卡
        if (m_ActiveLevel && m_ActiveLevel != level) {
            m_ActiveLevel->Deactivate();
        }

        // 激活新关卡
        if (!level->Activate()) return false;

        m_ActiveLevel = level;
        if (m_OnLevelActivated) {
            m_OnLevelActivated(name);
        }
        return true;
    }

    std::string LevelManager::GetActiveLevelName() const {
        return m_ActiveLevel ? m_ActiveLevel->GetName() : "";
    }

    // ═══════════════════════════════════════════════════════════
    // 关卡叠加（Additive）
    // ═══════════════════════════════════════════════════════════

    bool LevelManager::LoadLevelAdditive(const std::string& name) {
        Level* level = FindLevel(name);
        if (!level) {
            ENGINE_LOG_ERROR("LevelManager", "Cannot load additive '{}': not registered", name);
            return false;
        }

        // 如果已加载为叠加关卡，跳过
        if (std::find(m_AdditiveLevels.begin(), m_AdditiveLevels.end(), level) != m_AdditiveLevels.end()) {
            return true;
        }

        // 如果已加载为主关卡，也跳过
        if (m_ActiveLevel == level) return true;

        if (!LoadLevel(name)) return false;
        if (!level->Activate()) return false;

        m_AdditiveLevels.push_back(level);
        ENGINE_LOG_INFO("LevelManager", "Additive level loaded: '{}'", name);
        return true;
    }

    std::vector<Level*> LevelManager::GetAdditiveLevels() const {
        return m_AdditiveLevels;
    }

    // ═══════════════════════════════════════════════════════════
    // 异步加载
    // ═══════════════════════════════════════════════════════════

    void LevelManager::LoadLevelAsync(const std::string& name,
                                       std::function<void(bool)> onComplete) {
        Level* level = FindLevel(name);
        if (!level) {
            if (onComplete) onComplete(false);
            return;
        }

        m_IsLoading = true;
        m_LoadProgress = 0.0f;

        // 先加载依赖关卡（同步 + 递归）
        for (const auto& dep : level->GetInfo().dependencies) {
            if (!LoadLevel(dep)) {
                ENGINE_LOG_ERROR("LevelManager", "Async load: dependency '{}' failed for '{}'", dep, name);
                if (onComplete) onComplete(false);
                m_IsLoading = false;
                return;
            }
        }

        level->LoadAsync([this, name, onComplete](bool success) {
            m_IsLoading = false;
            m_LoadProgress = 1.0f;
            if (success && m_OnLevelLoaded) {
                m_OnLevelLoaded(name);
            }
            if (onComplete) onComplete(success);
        });
    }

    float LevelManager::GetLoadProgress() const {
        // 如果有正在异步加载的关卡，取所有关卡的平均进度
        float total = 0.0f;
        uint32 count = 0;
        for (auto& [_, level] : m_Levels) {
            if (level->GetState() == LevelState::Loading ||
                level->GetState() == LevelState::Loaded) {
                total += level->GetLoadProgress();
                count++;
            }
        }
        if (count == 0) return m_LoadProgress;
        return total / static_cast<float>(count);
    }

    // ═══════════════════════════════════════════════════════════
    // 每帧更新
    // ═══════════════════════════════════════════════════════════

    void LevelManager::Update(float32 dt) {
        if (m_ActiveLevel) m_ActiveLevel->Update(dt);
        for (auto* level : m_AdditiveLevels) {
            level->Update(dt);
        }
    }

    void LevelManager::Render() {
        if (m_ActiveLevel) m_ActiveLevel->Render();
        for (auto* level : m_AdditiveLevels) {
            level->Render();
        }
    }

    void LevelManager::PostPhysicsUpdate() {
        if (m_ActiveLevel) m_ActiveLevel->PostPhysicsUpdate();
        for (auto* level : m_AdditiveLevels) {
            level->PostPhysicsUpdate();
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 序列化
    // ═══════════════════════════════════════════════════════════

    bool LevelManager::SaveRegistry(const std::string& filePath) const {
        nlohmann::json root;
        nlohmann::json levels = nlohmann::json::array();

        for (const auto& [_, level] : m_Levels) {
            const auto& info = level->GetInfo();
            nlohmann::json j;
            j["name"]          = info.name;
            j["filePath"]      = info.filePath;
            j["category"]      = static_cast<int>(info.category);
            j["isPersistent"]  = info.isPersistent;
            j["loadPriority"]  = info.loadPriority;
            j["boundingRadius"] = info.boundingRadius;
            j["description"]   = info.description;
            j["thumbnailPath"] = info.thumbnailPath;
            // dependencies
            nlohmann::json deps = nlohmann::json::array();
            for (const auto& dep : info.dependencies) {
                deps.push_back(dep);
            }
            j["dependencies"] = deps;
            levels.push_back(j);
        }

        root["levels"] = levels;
        root["activeLevel"] = m_ActiveLevel ? m_ActiveLevel->GetName() : "";

        std::ofstream file(filePath);
        if (!file.is_open()) {
            ENGINE_LOG_ERROR("LevelManager", "Failed to save registry to {}", filePath);
            return false;
        }
        file << root.dump(4);
        file.close();
        ENGINE_LOG_INFO("LevelManager", "Registry saved to {}", filePath);
        return true;
    }

    bool LevelManager::LoadRegistry(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            ENGINE_LOG_WARN("LevelManager", "No registry file found at {}", filePath);
            return false;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("LevelManager", "Failed to parse registry: {}", e.what());
            return false;
        }

        m_Levels.clear();

        for (const auto& j : root["levels"]) {
            LevelInfo info;
            info.name          = j.value("name", "Unnamed");
            info.filePath      = j.value("filePath", "");
            info.category      = static_cast<LevelCategory>(j.value("category", 1));
            info.isPersistent  = j.value("isPersistent", false);
            info.loadPriority  = j.value("loadPriority", 0);
            info.boundingRadius = j.value("boundingRadius", 0.0f);
            info.description   = j.value("description", "");
            info.thumbnailPath = j.value("thumbnailPath", "");

            if (j.contains("dependencies")) {
                for (const auto& dep : j["dependencies"]) {
                    info.dependencies.push_back(dep);
                }
            }

            RegisterLevel(info);
        }

        // 恢复上次的活动关卡（但不自动激活）
        std::string activeName = root.value("activeLevel", "");
        if (!activeName.empty()) {
            ENGINE_LOG_INFO("LevelManager", "Last active level was '{}'", activeName);
        }

        ENGINE_LOG_INFO("LevelManager", "Registry loaded from {} ({} levels)", filePath, m_Levels.size());
        return true;
    }

} // namespace Engine