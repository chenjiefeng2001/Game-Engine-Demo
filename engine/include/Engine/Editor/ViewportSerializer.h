#pragma once

/**
 * @file ViewportSerializer.h
 * @brief ViewportConfig JSON 序列化 + Preset 预设管理
 *
 * 提供 ViewportConfig 与 JSON 之间的双向转换，以及预设库管理。
 * 使用 nlohmann/json 库，与 Scene JsonSerializer 一致。
 */

#include "Engine/Editor/ViewportConfig.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace Engine {

    /**
     * @brief 视口配置序列化器 — 将 ViewportConfig 读写为 JSON
     *
     * 保存/加载 Editor 设置到 engine/editor_settings.json。
     * 预设库以 memory 中的 std::map 形式存在。
     */
    class ViewportSerializer {
    public:
        // ═══════════════════════════════════════════════════════════
        // ViewportConfig ↔ JSON 转换
        // ═══════════════════════════════════════════════════════════

        /** 将 ViewportConfig 序列化为 JSON 对象 */
        static nlohmann::json Serialize(const ViewportConfig& config);

        /** 从 JSON 对象反序列化到 ViewportConfig */
        static ViewportConfig Deserialize(const nlohmann::json& json);

        /** 将 ViewportConfig 保存到 JSON 文件 */
        static bool SaveToFile(const ViewportConfig& config,
                               const std::string& filePath);

        /** 从 JSON 文件加载 ViewportConfig */
        static bool LoadFromFile(ViewportConfig& config,
                                 const std::string& filePath);

        // ═══════════════════════════════════════════════════════════
        // 多视口布局序列化（保存/恢复所有视口 + 排列）
        // ═══════════════════════════════════════════════════════════

        /** 将多个视口配置序列化为 JSON 数组 */
        static nlohmann::json SerializeLayout(
            const std::vector<ViewportConfig>& viewports);

        /** 从 JSON 数组反序列化多个视口配置 */
        static std::vector<ViewportConfig> DeserializeLayout(
            const nlohmann::json& json);

        // ═══════════════════════════════════════════════════════════
        // 预设系统 (Presets)
        // ═══════════════════════════════════════════════════════════

        using PresetMap = std::unordered_map<std::string, ViewportConfig>;

        /** 保存预设库到 JSON 文件 */
        static bool SavePresetsToFile(const PresetMap& presets,
                                      const std::string& filePath);

        /** 从 JSON 文件加载预设库 */
        static PresetMap LoadPresetsFromFile(const std::string& filePath);

        /** 获取内置默认预设（Level Design / Physics Debug / Lighting 等） */
        static PresetMap GetDefaultPresets();

        // ═══════════════════════════════════════════════════════════
        // 编辑器设置文件管理（engine/editor_settings.json）
        // ═══════════════════════════════════════════════════════════

        /** 编辑器全部设置 */
        struct EditorSettings {
            std::vector<ViewportConfig> viewports;   ///< 所有打开的视口
            std::string activePreset;                 ///< 当前激活的预设名称
            float uiScale = 1.0f;                     ///< UI 缩放比
        };

        /** 将编辑器设置保存到 JSON */
        static nlohmann::json SerializeSettings(const EditorSettings& settings);

        /** 从 JSON 加载编辑器设置 */
        static EditorSettings DeserializeSettings(const nlohmann::json& json);

        /** 保存编辑器设置到默认路径 (engine/editor_settings.json) */
        static bool SaveEditorSettings(const EditorSettings& settings);

        /** 从默认路径加载编辑器设置 */
        static EditorSettings LoadEditorSettings();

    private:
        // ── 内部辅助 ──
        static nlohmann::json SerializeCameraConfig(const CameraConfig& cam);
        static CameraConfig DeserializeCameraConfig(const nlohmann::json& json);
    };

} // namespace Engine