#pragma once

/**
 * @file PropertyAttributes.h
 * @brief 属性特性系统 — 为 Inspector 提供数据绑定元数据
 *
 * 参考 Unity 的 PropertyAttribute 系统，允许通过特性标记自动控制 UI 行为：
 *
 * @code
 *   class HealthComponent : public Component {
 *       // 自动生成 [0, 100] 滑块
 *       ENGINE_PROPERTY(DisplayName = "Max HP", Range = (0.0f, 100.0f))
 *       int maxHP = 100;
 *
 *       // 只读字段（Debug 模式下可见）
 *       ENGINE_PROPERTY(ReadOnly)
 *       float currentHP = 100.0f;
 *
 *       // 带有自定义绘制器的复杂类型
 *       ENGINE_PROPERTY(CustomDrawer = "CurveEditor")
 *       AnimationCurve damageCurve;
 *   };
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace Engine {

    // ============================================================
    // 属性特性标记 (Attribute Flags)
    // ============================================================
    enum class PropertyFlag : uint32 {
        None            = 0,
        ReadOnly        = 1 << 0,   ///< 只读（不可编辑）
        Hidden          = 1 << 1,   ///< 在 Inspector 中隐藏
        DebugOnly       = 1 << 2,   ///< 仅在 Debug 模式下显示
        ShowIf          = 1 << 3,   ///< 条件显示
        DisableIf       = 1 << 4,   ///< 条件禁用
        Range           = 1 << 5,   ///< 数值范围滑块
        Slider          = 1 << 6,   ///< 拖拽滑块
        Min             = 1 << 7,   ///< 最小值约束
        Max             = 1 << 8,   ///< 最大值约束
        Step            = 1 << 9,   ///< 步长
        Expression      = 1 << 10,  ///< 支持数学表达式输入
        AssetReference  = 1 << 11,  ///< 资产引用（拖拽/选择器）
        Color           = 1 << 12,  ///< 颜色拾取
        Gradient        = 1 << 13,  ///< 渐变编辑器
        Curve           = 1 << 14,  ///< 曲线编辑器
        FlagsEnum       = 1 << 15,  ///< 标志位枚举（Bitmask）
        TextArea        = 1 << 16,  ///< 多行文本
        Password        = 1 << 17,  ///< 密码字段
        FilePath        = 1 << 18,  ///< 文件路径选择器
        FolderPath      = 1 << 19,  ///< 文件夹路径选择器
        Button          = 1 << 20,  ///< 内联按钮
        Foldout         = 1 << 21,  ///< 折叠组
        Header          = 1 << 22,  ///< 分组标题
        Space           = 1 << 23,  ///< 空白间距
        Tooltip         = 1 << 24,  ///< 工具提示
        Unit            = 1 << 25,  ///< 单位后缀（如 "kg", "m/s"）
        Precision       = 1 << 26,  ///< 小数位数
    };

    inline PropertyFlag operator|(PropertyFlag a, PropertyFlag b) {
        return static_cast<PropertyFlag>(
            static_cast<uint32>(a) | static_cast<uint32>(b));
    }
    inline PropertyFlag operator&(PropertyFlag a, PropertyFlag b) {
        return static_cast<PropertyFlag>(
            static_cast<uint32>(a) & static_cast<uint32>(b));
    }
    inline bool HasFlag(PropertyFlag flags, PropertyFlag flag) {
        return (static_cast<uint32>(flags) & static_cast<uint32>(flag)) != 0;
    }

    // ============================================================
    // 属性范围描述
    // ============================================================
    struct PropertyRange {
        float min = 0.0f;
        float max = 1.0f;
        float step = 0.01f;
    };

    // ============================================================
    // 属性元数据（由特性系统生成，供 Inspector 使用）
    // ============================================================
    struct PropertyMeta {
        std::string displayName;        ///< 显示名称
        std::string tooltip;            ///< 工具提示
        std::string unit;               ///< 单位后缀
        std::string customDrawer;       ///< 自定义绘制器名称
        PropertyFlag flags = PropertyFlag::None;
        PropertyRange range;
        float   minValue   = 0.0f;
        float   maxValue   = 0.0f;
        float   step       = 0.0f;
        int     precision  = 2;
        bool    showIf     = true;      ///< 条件显示的结果
        bool    enabledIf  = true;      ///< 条件启用的结果
        uint32_t groupId   = 0;         ///< 分组 ID（用于 Foldout）

        // 条件显示/禁用的属性名
        std::string conditionalProperty;
    };

    // ============================================================
    // 属性值访问器 — 泛化属性的 Get/Set 操作
    // ============================================================
    enum class PropertyType : uint8 {
        Bool,
        Int8, Int16, Int32, Int64,
        UInt8, UInt16, UInt32, UInt64,
        Float, Double,
        String,
        Vec2, Vec3, Vec4,
        Color,
        Enum,
        FlagsEnum,
        AssetRef,
        ObjectRef,
        Custom
    };

    /// 属性值联合体
    union PropertyValue {
        bool    boolVal;
        int32   int32Val;
        int64   int64Val;
        uint32  uint32Val;
        uint64  uint64Val;
        float   floatVal;
        double  doubleVal;
    };

    /// 属性访问器：抽象了对 GameObject/Component 上某个属性的读写
    struct PropertyAccessor {
        std::string name;
        PropertyType type = PropertyType::Float;

        // 读写回调
        std::function<PropertyValue(void*)> getter;   ///< 参数为对象指针
        std::function<void(void*, PropertyValue)> setter;

        // 特殊类型的读写
        std::function<std::string(void*)> stringGetter;
        std::function<void(void*, const std::string&)> stringSetter;

        // 枚举
        std::function<int(void*)> enumGetter;
        std::function<void(void*, int)> enumSetter;
        std::vector<std::pair<int, std::string>> enumValues;

        // Vec
        std::function<void(void*, Vec3&)> vec3Getter;
        std::function<void(void*, const Vec3&)> vec3Setter;
        std::function<void(void*, Vec4&)> vec4Getter;
        std::function<void(void*, const Vec4&)> vec4Setter;

        // 对象引用
        std::function<void*(void*)> objectGetter;
        std::function<void(void*, void*)> objectSetter;

        PropertyMeta meta;
    };

    // ============================================================
    // 组件绘制上下文 — 传递给自定义绘制器的环境信息
    // ============================================================
    struct DrawContext {
        void*       objectPtr   = nullptr;   ///< 正在绘制的对象指针
        uint64      objectId    = 0;          ///< 对象唯一 ID
        bool        debugMode   = false;      ///< 是否处于 Debug 模式
        bool        isMultiSelect = false;    ///< 是否正在多选编辑
        bool        isPrefabOverride = false; ///< 是否有预制体覆盖
        bool        isReadOnly  = false;      ///< 是否只读模式

        // 撤销支持
        std::function<void()> recordUndo;     ///< 记录撤销快照

        // 搜索过滤
        std::string searchFilter;             ///< 当前搜索文本
    };

} // namespace Engine