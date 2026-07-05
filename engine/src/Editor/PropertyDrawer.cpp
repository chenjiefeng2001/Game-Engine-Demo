#include "Engine/Editor/PropertyDrawer.h"
#include "Engine/Core/Log.h"
#include <imgui.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace Engine { namespace Inspector {

    // ============================================================
    // 表达式求值（轻量级数学解析器）
    // ============================================================
    namespace {
        /// 简单的表达式求值器，支持 + - * / ( ) 和浮点数
        class SimpleExpressionEval {
        public:
            static double Evaluate(const char* expr) {
                if (!expr || !*expr) return 0.0;
                std::string s(expr);
                // 移除空格
                s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
                if (s.empty()) return 0.0;

                const char* p = s.c_str();
                double result = parseExpr(p);
                if (*p != '\0') {
                    // 如果仍有未解析字符，尝试作为普通数值返回
                    return std::atof(expr);
                }
                return result;
            }

        private:
            static double parseExpr(const char*& p) {
                double v = parseTerm(p);
                while (*p == '+' || *p == '-') {
                    char op = *p++;
                    double rhs = parseTerm(p);
                    v = (op == '+') ? v + rhs : v - rhs;
                }
                return v;
            }

            static double parseTerm(const char*& p) {
                double v = parseFactor(p);
                while (*p == '*' || *p == '/') {
                    char op = *p++;
                    double rhs = parseFactor(p);
                    if (op == '*') v *= rhs;
                    else if (rhs != 0.0) v /= rhs;
                }
                return v;
            }

            static double parseFactor(const char*& p) {
                if (*p == '(') {
                    ++p;
                    double v = parseExpr(p);
                    if (*p == ')') ++p;
                    return v;
                }
                if (*p == '-') {
                    ++p;
                    return -parseFactor(p);
                }
                // 解析数字
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end != p) {
                    p = end;
                    return v;
                }
                return 0.0;
            }
        };

        bool processExpression(const char* label, const char* input, float* outValue) {
            double result = SimpleExpressionEval::Evaluate(input);
            *outValue = static_cast<float>(result);
            return true;
        }

        bool processExpressionInt(const char* label, const char* input, int* outValue) {
            double result = SimpleExpressionEval::Evaluate(input);
            *outValue = static_cast<int>(std::round(result));
            return true;
        }

        // 绘制标签 + 工具提示 + 单位后缀
        void drawLabel(const char* label, const PropertyMeta& meta,
                       const DrawConfig& config) {
            if (!label || !*label) return;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // 工具提示图标
            if (config.showTooltip && !meta.tooltip.empty()) {
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(meta.tooltip.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::SameLine();
            }

            // 标签
            ImGui::TextUnformatted(label);

            // 单位后缀
            if (config.showUnit && !meta.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", meta.unit.c_str());
            }

            ImGui::TableNextColumn();
        }
    }

    // ============================================================
    // 布局辅助实现
    // ============================================================

    void BeginPropertyRow(const char* label, float labelWidth) {
        ImGui::PushID(label);
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, labelWidth);
        ImGui::TextUnformatted(label);
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
    }

    void EndPropertyRow() {
        ImGui::PopItemWidth();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void DrawPropertyLabel(const char* label, const std::string& searchFilter) {
        if (!searchFilter.empty()) {
            // 搜索高亮
            const char* found = std::strstr(label, searchFilter.c_str());
            if (found) {
                // 在匹配文本前后绘制不同颜色
                std::string before(label, found - label);
                std::string match(found, searchFilter.size());
                std::string after(found + searchFilter.size());

                if (!before.empty()) ImGui::TextUnformatted(before.c_str());
                ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                ImGui::TextUnformatted(match.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 0);
                if (!after.empty()) ImGui::TextUnformatted(after.c_str());
                return;
            }
        }
        ImGui::TextUnformatted(label);
    }

    bool DrawGroupHeader(const char* label, bool defaultOpen) {
        return ImGui::CollapsingHeader(label,
            defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    }

    bool DrawComponentHeader(const char* name, bool* enabled,
                             std::function<void()> contextMenu) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));

        // 行背景
        ImVec2 headerMin = ImGui::GetCursorScreenPos();
        ImVec2 headerMax = ImVec2(headerMin.x + ImGui::GetContentRegionAvail().x,
                                  headerMin.y + ImGui::GetFrameHeightWithSpacing());
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(headerMin, headerMax,
                                IM_COL32(60, 60, 60, 255));

        // 启用/禁用复选框
        bool changed = false;
        if (enabled) {
            ImGui::SetCursorScreenPos(ImVec2(headerMin.x + 4, headerMin.y + 2));
            changed = ImGui::Checkbox("##enabled", enabled);
            ImGui::SameLine();
        }

        // 标题（用不同颜色区分启用/禁用）
        if (enabled && !*enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }

        bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine();

        // 设置齿轮菜单
        if (contextMenu) {
            float lineHeight = ImGui::GetFrameHeight();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - lineHeight - 8);
            if (ImGui::Button("...", ImVec2(lineHeight, lineHeight))) {
                ImGui::OpenPopup("##component_menu");
            }
            if (ImGui::BeginPopup("##component_menu")) {
                contextMenu();
                ImGui::EndPopup();
            }
        }

        if (enabled && !*enabled) {
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();

        if (open) {
            ImGui::Indent();
        }

        return open;
    }

    void DrawPrefabOverrideMarker(bool isOverridden) {
        if (!isOverridden) return;
        // 蓝色左侧条
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        drawList->AddRectFilled(
            ImVec2(pos.x - 16, pos.y),
            ImVec2(pos.x - 12, pos.y + ImGui::GetTextLineHeight()),
            IM_COL32(70, 130, 255, 180));
    }

    void DrawMixedValuePlaceholder() {
        ImGui::TextUnformatted("---");
    }

    void DrawReadOnlyMarker() {
        ImGui::TextDisabled("(lock)");
    }

    void DrawPropertyContextMenu(const char* propertyPath,
                                 bool isPrefabOverride,
                                 std::function<void()> onRevert) {
        if (ImGui::BeginPopupContextItem(propertyPath)) {
            if (isPrefabOverride && onRevert) {
                if (ImGui::MenuItem("Revert to Prefab")) {
                    onRevert();
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Copy")) {
                // ImGui::SetClipboardText(propertyPath);
            }
            if (ImGui::MenuItem("Paste")) {
                // 从剪贴板读取
            }
            ImGui::EndPopup();
        }
    }

    // ============================================================
    // 数值字段实现
    // ============================================================

    bool DrawFloatField(const char* label, float* value,
                        const PropertyMeta& meta,
                        const DrawConfig& config) {
        BeginPropertyRow(label, config.labelWidth);

        bool changed = false;
        char buf[64];

        if (HasFlag(meta.flags, PropertyFlag::Range) ||
            (meta.maxValue > meta.minValue)) {
            // 范围滑块
            float minVal = meta.range.min;
            float maxVal = meta.range.max;
            if (maxVal < minVal) { minVal = 0.0f; maxVal = 1.0f; }
            changed = ImGui::SliderFloat("##value", value, minVal, maxVal,
                                          "%.3f", ImGuiSliderFlags_AlwaysClamp);
        } else if (HasFlag(meta.flags, PropertyFlag::Expression) &&
                   config.expressionInput) {
            // 表达式输入
            std::snprintf(buf, sizeof(buf), "%.*f", meta.precision, *value);
            if (ImGui::InputText("##expr", buf, sizeof(buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                float newVal = *value;
                if (processExpression(label, buf, &newVal)) {
                    // 应用范围约束
                    if (meta.maxValue > meta.minValue)
                        newVal = std::clamp(newVal, meta.minValue, meta.maxValue);
                    if (std::abs(newVal - *value) > 0.0001f) {
                        *value = newVal;
                        changed = true;
                    }
                }
            }
            // 工具提示：支持表达式
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Supports expressions: 10+5, 1920/2, (3*4)+2");
                ImGui::EndTooltip();
            }
        } else {
            // 默认拖拽
            float speed = meta.step > 0.0f ? meta.step : 0.1f;
            float minVal = HasFlag(meta.flags, PropertyFlag::Min) ? meta.minValue : 0.0f;
            float maxVal = HasFlag(meta.flags, PropertyFlag::Max) ? meta.maxValue : 0.0f;
            changed = ImGui::DragFloat("##value", value, speed, minVal, maxVal,
                                       "%.*f", meta.precision);
        }

        EndPropertyRow();
        return changed;
    }

    bool DrawIntField(const char* label, int* value,
                      const PropertyMeta& meta,
                      const DrawConfig& config) {
        BeginPropertyRow(label, config.labelWidth);

        bool changed = false;
        char buf[64];

        if (HasFlag(meta.flags, PropertyFlag::Range) ||
            (meta.maxValue > meta.minValue)) {
            int minVal = static_cast<int>(meta.range.min);
            int maxVal = static_cast<int>(meta.range.max);
            changed = ImGui::SliderInt("##value", value, minVal, maxVal);
        } else if (HasFlag(meta.flags, PropertyFlag::Expression) &&
                   config.expressionInput) {
            std::snprintf(buf, sizeof(buf), "%d", *value);
            if (ImGui::InputText("##expr", buf, sizeof(buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                int newVal = *value;
                if (processExpressionInt(label, buf, &newVal)) {
                    if (newVal != *value) {
                        *value = newVal;
                        changed = true;
                    }
                }
            }
        } else {
            float speed = meta.step > 0.0f ? meta.step : 1.0f;
            int minVal = HasFlag(meta.flags, PropertyFlag::Min) ?
                         static_cast<int>(meta.minValue) : 0;
            int maxVal = HasFlag(meta.flags, PropertyFlag::Max) ?
                         static_cast<int>(meta.maxValue) : 0;
            changed = ImGui::DragInt("##value", value, speed, minVal, maxVal);
        }

        EndPropertyRow();
        return changed;
    }

    bool DrawDragFloat(const char* label, float* value,
                       float speed, float min, float max,
                       const char* format) {
        BeginPropertyRow(label);
        bool changed = ImGui::DragFloat("##value", value, speed, min, max, format);
        EndPropertyRow();
        return changed;
    }

    bool DrawDragInt(const char* label, int* value,
                     float speed, int min, int max) {
        BeginPropertyRow(label);
        bool changed = ImGui::DragInt("##value", value, speed, min, max);
        EndPropertyRow();
        return changed;
    }

    bool DrawExpressionFloat(const char* label, float* value,
                             const PropertyMeta& meta) {
        DrawConfig cfg;
        cfg.expressionInput = true;
        return DrawFloatField(label, value, meta, cfg);
    }

    bool DrawExpressionInt(const char* label, int* value,
                           const PropertyMeta& meta) {
        DrawConfig cfg;
        cfg.expressionInput = true;
        return DrawIntField(label, value, meta, cfg);
    }

    // ============================================================
    // 颜色与渐变
    // ============================================================

    bool DrawColorField(const char* label, Vec4* color,
                        bool showAlpha, const PropertyMeta& meta) {
        BeginPropertyRow(label);
        int flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                    ImGuiColorEditFlags_AlphaPreview;
        if (!showAlpha) {
            flags |= ImGuiColorEditFlags_NoAlpha;
        }
        bool changed = ImGui::ColorEdit4("##color", &color->x, flags);
        EndPropertyRow();
        return changed;
    }

    bool DrawColorFieldRGB(const char* label, Vec3* color,
                           const PropertyMeta& meta) {
        BeginPropertyRow(label);
        bool changed = ImGui::ColorEdit3("##color", &color->x,
                                          ImGuiColorEditFlags_NoInputs);
        EndPropertyRow();
        return changed;
    }

    bool DrawGradientField(const char* label,
                           std::function<void()> gradientEditorFn,
                           const PropertyMeta& meta) {
        BeginPropertyRow(label);
        // 渐变预览条
        ImVec2 rectMin = ImGui::GetCursorScreenPos();
        ImVec2 rectMax = ImVec2(rectMin.x + ImGui::GetContentRegionAvail().x,
                                rectMin.y + 20);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // 绘制渐变预览（简化实现）
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(100, 100, 200, 255));
        drawList->AddRect(rectMin, rectMax, IM_COL32(200, 200, 200, 128));

        ImGui::SetCursorScreenPos(ImVec2(rectMin.x, rectMax.y + 4));

        if (ImGui::Button("Edit Gradient", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            if (gradientEditorFn) gradientEditorFn();
        }
        EndPropertyRow();
        return false;
    }

    // ============================================================
    // 枚举与标志位
    // ============================================================

    bool DrawEnumField(const char* label, int* current,
                       const int* values, const char** names,
                       int count, const PropertyMeta& meta) {
        BeginPropertyRow(label);

        bool changed = false;
        // 查找当前值的索引
        int currentIndex = -1;
        for (int i = 0; i < count; ++i) {
            if (values[i] == *current) {
                currentIndex = i;
                break;
            }
        }
        if (currentIndex < 0) currentIndex = 0;

        const char* preview = (currentIndex >= 0 && currentIndex < count)
                              ? names[currentIndex] : "Unknown";

        if (ImGui::BeginCombo("##enum", preview)) {
            for (int i = 0; i < count; ++i) {
                bool isSelected = (i == currentIndex);
                if (ImGui::Selectable(names[i], isSelected)) {
                    *current = values[i];
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        EndPropertyRow();
        return changed;
    }

    bool DrawFlagsEnumField(const char* label, uint32* flags,
                            const char** optionNames,
                            int optionCount, const PropertyMeta& meta) {
        BeginPropertyRow(label);

        bool changed = false;
        if (ImGui::BeginCombo("##flags", "...")) {
            for (int i = 0; i < optionCount; ++i) {
                uint32 bit = 1u << i;
                bool checked = (*flags & bit) != 0;
                if (ImGui::Checkbox(optionNames[i], &checked)) {
                    if (checked) *flags |= bit;
                    else *flags &= ~bit;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        // 显示选中数量
        int count = 0;
        uint32 f = *flags;
        while (f) { count += f & 1; f >>= 1; }
        if (count > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d selected)", count);
        }

        EndPropertyRow();
        return changed;
    }

    bool DrawBoolField(const char* label, bool* value,
                       const PropertyMeta& meta) {
        BeginPropertyRow(label);
        bool changed = ImGui::Checkbox("##bool", value);
        EndPropertyRow();
        return changed;
    }

    // ============================================================
    // 字符串与文本
    // ============================================================

    bool DrawTextField(const char* label, char* buffer, size_t bufferSize,
                       const PropertyMeta& meta) {
        BeginPropertyRow(label);
        int flags = ImGuiInputTextFlags_None;
        if (HasFlag(meta.flags, PropertyFlag::Password))
            flags |= ImGuiInputTextFlags_Password;
        bool changed = ImGui::InputText("##text", buffer, bufferSize, flags);
        EndPropertyRow();
        return changed;
    }

    bool DrawTextArea(const char* label, char* buffer, size_t bufferSize,
                      float height, const PropertyMeta& meta) {
        BeginPropertyRow(label);
        ImGui::BeginChild("##textarea", ImVec2(0, height), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        bool changed = ImGui::InputTextMultiline("##area", buffer, bufferSize,
                                                  ImVec2(-1, -1));
        ImGui::EndChild();
        EndPropertyRow();
        return changed;
    }

    // ============================================================
    // 资产引用
    // ============================================================

    bool DrawAssetRefField(const char* label,
                           std::string* assetName,
                           std::function<void()> onPick,
                           std::function<void(const char*)> onDrop,
                           std::function<void()> onPreview,
                           const PropertyMeta& meta) {
        BeginPropertyRow(label);

        bool changed = false;

        // 拖拽目标
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

        // 显示资产名称
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", assetName->c_str());
        ImGui::InputText("##asset", buf, sizeof(buf),
                         ImGuiInputTextFlags_ReadOnly);

        ImGui::PopStyleColor();

        // 拖拽目标区域
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
                if (onDrop && payload->Data) {
                    onDrop(static_cast<const char*>(payload->Data));
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 悬停预览
        if (ImGui::IsItemHovered() && onPreview) {
            onPreview();
        }

        // 小圆圈选择器
        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(24, 0))) {
            if (onPick) onPick();
        }

        // 清除按钮
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(20, 0))) {
            *assetName = "None";
            changed = true;
        }

        EndPropertyRow();
        return changed;
    }

    // ============================================================
    // 向量字段
    // ============================================================

    bool DrawVec2Field(const char* label, float v[2],
                       float speed, const PropertyMeta& meta) {
        BeginPropertyRow(label);
        bool changed = ImGui::DragFloat2("##vec2", v, speed);
        EndPropertyRow();
        return changed;
    }

    bool DrawVec3Field(const char* label, float v[3],
                       float speed, float min, float max,
                       const PropertyMeta& meta) {
        // 复用现有的 Ui::DrawVec3Control
        // 这里用 ImGui 原生实现
        BeginPropertyRow(label);
        bool changed = ImGui::DragFloat3("##vec3", v, speed, min, max);
        EndPropertyRow();
        return changed;
    }

    bool DrawVec4Field(const char* label, float v[4],
                       float speed, const PropertyMeta& meta) {
        BeginPropertyRow(label);
        bool changed = ImGui::DragFloat4("##vec4", v, speed);
        EndPropertyRow();
        return changed;
    }

}} // namespace Engine::Inspector