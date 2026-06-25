#pragma once

/**
 * @file PropertyDrawer.h
 * @brief 增强型属性绘制器 — 工业级字段组件系统
 *
 * 提供丰富的字段绘制组件，支持：
 *   - 带表达式输入的数值字段（输入 "10+5" 自动计算）
 *   - 范围滑块 [Range(0, 1)]
 *   - 颜色拾取器 / 渐变编辑器
 *   - 标志位枚举（Flags Enum Bitmask）
 *   - 资产引用拖拽与选择器
 *   - 曲线编辑器
 *   - 预制体覆盖标记
 *   - 多选编辑混合值显示
 */

#include "Engine/Types.h"
#include "Engine/Editor/PropertyAttributes.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <functional>
#include <vector>
#include <imgui.h>

namespace Engine { namespace Inspector {

    // ============================================================
    // 绘制配置
    // ============================================================
    struct DrawConfig {
        bool    showTooltip     = true;
        bool    showUnit        = true;
        bool    expressionInput = true;  ///< 支持数学表达式
        float   labelWidth      = 120.0f;
        float   itemWidth       = -1.0f;  ///< -1 = fill remaining
        bool    compactMode     = false;  ///< 紧凑模式
    };

    // ============================================================
    // 数值字段（带表达式支持）
    // ============================================================

    /**
     * @brief 带表达式输入和范围约束的浮点字段
     * @param label    标签
     * @param value    当前值（将被修改）
     * @param meta     属性元数据（范围、步长、单位等）
     * @param config   绘制配置
     * @return true    如果值被修改
     */
    bool DrawFloatField(const char* label, float* value,
                        const PropertyMeta& meta = PropertyMeta{},
                        const DrawConfig& config = DrawConfig{});

    /**
     * @brief 带表达式输入的整数字段
     */
    bool DrawIntField(const char* label, int* value,
                      const PropertyMeta& meta = PropertyMeta{},
                      const DrawConfig& config = DrawConfig{});

    /**
     * @brief 拖拽浮点（带步长和范围）
     */
    bool DrawDragFloat(const char* label, float* value,
                       float speed = 0.1f,
                       float min = 0.0f, float max = 0.0f,
                       const char* format = "%.2f");

    /**
     * @brief 拖拽整数
     */
    bool DrawDragInt(const char* label, int* value,
                     float speed = 1.0f,
                     int min = 0, int max = 0);

    /**
     * @brief 带表达式的整数输入
     */
    bool DrawExpressionInt(const char* label, int* value,
                           const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 带表达式的浮点输入
     */
    bool DrawExpressionFloat(const char* label, float* value,
                             const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 颜色与渐变
    // ============================================================

    /**
     * @brief 颜色拾取器（带 Alpha 通道）
     */
    bool DrawColorField(const char* label, Vec4* color,
                        bool showAlpha = true,
                        const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 颜色拾取器（RGB，无 Alpha）
     */
    bool DrawColorFieldRGB(const char* label, Vec3* color,
                           const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 渐变条绘制
     */
    bool DrawGradientField(const char* label,
                           std::function<void()> gradientEditorFn,
                           const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 枚举与标志位
    // ============================================================

    /**
     * @brief 枚举下拉选择
     * @param label    标签
     * @param current  当前选中值（将被修改）
     * @param values   枚举值列表
     * @param names    显示名称列表
     * @return true    如果值被修改
     */
    bool DrawEnumField(const char* label, int* current,
                       const int* values, const char** names,
                       int count,
                       const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 标志位枚举（Bitmask）— 多选下拉/复选框组
     */
    bool DrawFlagsEnumField(const char* label, uint32* flags,
                            const char** optionNames,
                            int optionCount,
                            const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 布尔字段（复选框）
     */
    bool DrawBoolField(const char* label, bool* value,
                       const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 字符串与文本
    // ============================================================

    /**
     * @brief 单行文本输入
     */
    bool DrawTextField(const char* label, char* buffer, size_t bufferSize,
                       const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief 多行文本输入
     */
    bool DrawTextArea(const char* label, char* buffer, size_t bufferSize,
                      float height = 100.0f,
                      const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 资产引用
    // ============================================================

    /**
     * @brief 资产引用字段（带拖拽目标 + 小圆圈选择器 + 悬停预览）
     * @param label       标签
     * @param assetName   当前资产名称（可拖拽修改）
     * @param onPick      点击选择器回调
     * @param onDrop      拖拽资产回调
     * @param onPreview   悬停预览回调（可选）
     * @return true       如果资产被修改
     */
    bool DrawAssetRefField(const char* label,
                           std::string* assetName,
                           std::function<void()> onPick,
                           std::function<void(const char*)> onDrop,
                           std::function<void()> onPreview = nullptr,
                           const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 向量与矩阵
    // ============================================================

    /**
     * @brief Vec2 字段（两个并排输入框）
     */
    bool DrawVec2Field(const char* label, float v[2],
                       float speed = 0.1f,
                       const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief Vec3 字段（三个并排输入框，带颜色标签）
     */
    bool DrawVec3Field(const char* label, float v[3],
                       float speed = 0.1f,
                       float min = 0.0f, float max = 0.0f,
                       const PropertyMeta& meta = PropertyMeta{});

    /**
     * @brief Vec4 字段（四个并排输入框）
     */
    bool DrawVec4Field(const char* label, float v[4],
                       float speed = 0.1f,
                       const PropertyMeta& meta = PropertyMeta{});

    // ============================================================
    // 状态可视化
    // ============================================================

    /**
     * @brief 绘制预制体覆盖标记（蓝色左侧条 + 加粗标签）
     */
    void DrawPrefabOverrideMarker(bool isOverridden);

    /**
     * @brief 绘制多选混合值占位符（"---"）
     */
    void DrawMixedValuePlaceholder();

    /**
     * @brief 绘制只读标记（小锁图标）
     */
    void DrawReadOnlyMarker();

    /**
     * @brief 绘制属性上下文菜单（还原预制体、复制/粘贴值）
     */
    void DrawPropertyContextMenu(const char* propertyPath,
                                 bool isPrefabOverride,
                                 std::function<void()> onRevert);

    // ============================================================
    // 布局辅助
    // ============================================================

    /**
     * @brief 绘制组件标题栏（带启用/禁用 Toggle + 设置菜单）
     */
    bool DrawComponentHeader(const char* name,
                             bool* enabled,
                             std::function<void()> contextMenu = nullptr);

    /**
     * @brief 开始属性绘制行（处理标签 + 左对齐）
     */
    void BeginPropertyRow(const char* label, float labelWidth = 120.0f);

    /**
     * @brief 结束属性绘制行
     */
    void EndPropertyRow();

    /**
     * @brief 绘制带搜索高亮的属性名
     */
    void DrawPropertyLabel(const char* label, const std::string& searchFilter = "");

    /**
     * @brief 绘制组折叠标题
     */
    bool DrawGroupHeader(const char* label, bool defaultOpen = true);

}} // namespace Engine::Inspector