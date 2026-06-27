#pragma once

/**
 * @file Reflect.h
 * @brief 轻量级 C++ 反射系统 — 自动化属性 UI 生成
 *
 * 设计目标：
 *   1. 新增组件无需手动写 ImGui 代码
 *   2. 通过模板注册机制在编译期绑定成员变量偏移
 *   3. InspectorPanel 根据 MetaClass 自动生成 UI
 *
 * 使用方式：
 * @code
 *   class PointLightComponent : public Component {
 *   public:
 *       float Radius = 10.0f;
 *       Vec3 Color = {1, 1, 1};
 *
 *       static void RegisterReflection() {
 *           Reflect::MetaBuilder<PointLightComponent>("PointLight")
 *               .Field("Radius", &PointLightComponent::Radius, Reflect::Range(0.1f, 100.0f))
 *               .Field("Color", &PointLightComponent::Color, Reflect::ColorAttr());
 *       }
 *   };
 *
 *   // 文件底部注册（静态初始化）
 *   REFLECT_REGISTER(PointLightComponent);
 * @endcode
 *
 * Inspector 自动遍历所有已注册组件并绘制 UI：
 * @code
 *   void InspectorPanel::AutoDrawComponent(Component* comp) {
 *       auto* meta = Reflect::GetClass(typeid(*comp));
 *       if (!meta) return;  // 未注册反射，使用回退显示
 *       for (const auto& field : meta->GetFields()) {
 *           DrawFieldAuto(comp, field);
 *       }
 *   }
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeinfo>
#include <typeindex>
#include <functional>

namespace Engine {
namespace Reflect {

    // ═══════════════════════════════════════════════════════════════
    // 字段类型
    // ═══════════════════════════════════════════════════════════════
    enum class FieldType : uint8 {
        Float,
        Int,
        UInt32,
        Bool,
        Vec2,
        Vec3,
        Vec4,
        Color,          // Vec4 treated as Color with alpha
        ColorRGB,       // Vec3 treated as Color
        String,
        Enum,
        AssetRef,       // Texture/Sound/etc. path reference
    };

    // ═══════════════════════════════════════════════════════════════
    // 属性约束
    // ═══════════════════════════════════════════════════════════════
    struct Range {
        float min = 0.0f;
        float max = 0.0f;
        float step = 0.1f;
        constexpr Range() = default;
        constexpr Range(float mn, float mx, float st = 0.1f)
            : min(mn), max(mx), step(st) {}
    };

    struct ColorAttr {
        bool withAlpha = true;
    };

    struct EnumAttr {
        const char** names = nullptr;
        int count = 0;
        int* values = nullptr;
    };

    struct AssetRefAttr {
        const char* assetType = "";  // "Texture", "AudioClip", etc.
    };

    // ═══════════════════════════════════════════════════════════════
    // 字段描述符
    // ═══════════════════════════════════════════════════════════════
    struct FieldDesc {
        std::string  name;        ///< 显示名称
        FieldType    type;
        
        // 成员偏移量（通过 offsetof 或 pointer-to-member 获取）
        size_t       offset = 0;  

        // 属性约束（可选）
        Range        range;
        bool         isColor     = false;
        bool         colorAlpha  = true;
        EnumAttr     enumAttr;
        AssetRefAttr assetAttr;

        // 便捷取值/设值模板（由 MetaClass 实现）
        template<typename T, typename FieldT>
        static size_t CalcOffset(FieldT T::*member) {
            return reinterpret_cast<size_t>(&(reinterpret_cast<T const volatile*>(0)->*member));
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // 类元数据 — 描述一个可反射的类
    // ═══════════════════════════════════════════════════════════════
    class MetaClass {
    public:
        MetaClass(const char* className) : m_ClassName(className) {}

        const std::string& GetName() const { return m_ClassName; }
        const std::vector<FieldDesc>& GetFields() const { return m_Fields; }

        template<typename T>
        MetaClass& Field(const char* name, float T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Float;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, int T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Int;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, uint32 T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::UInt32;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, bool T::*member) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Bool;
            fd.offset = FieldDesc::CalcOffset(member);
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, Vec2 T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Vec2;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, Vec3 T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Vec3;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        template<typename T>
        MetaClass& Field(const char* name, Vec4 T::*member, Range range = {}) {
            FieldDesc fd;
            fd.name   = name;
            fd.type   = FieldType::Vec4;
            fd.offset = FieldDesc::CalcOffset(member);
            fd.range  = range;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        // 颜色专用（Vec4 + ColorPicker）
        template<typename T>
        MetaClass& Color(const char* name, Vec4 T::*member, bool withAlpha = true) {
            FieldDesc fd;
            fd.name       = name;
            fd.type       = FieldType::Color;
            fd.offset     = FieldDesc::CalcOffset(member);
            fd.isColor    = true;
            fd.colorAlpha = withAlpha;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

        // 颜色 RGB（Vec3 + ColorPicker）
        template<typename T>
        MetaClass& Color(const char* name, Vec3 T::*member) {
            FieldDesc fd;
            fd.name       = name;
            fd.type       = FieldType::ColorRGB;
            fd.offset     = FieldDesc::CalcOffset(member);
            fd.isColor    = true;
            fd.colorAlpha = false;
            m_Fields.push_back(std::move(fd));
            return *this;
        }

    private:
        std::string m_ClassName;
        std::vector<FieldDesc> m_Fields;
    };

    // ═══════════════════════════════════════════════════════════════
    // 全局注册表
    // ═══════════════════════════════════════════════════════════════

    /** 注册一个 MetaClass */
    void RegisterClass(const std::type_index& typeIndex, std::unique_ptr<MetaClass> meta);

    /** 获取已注册的 MetaClass（返回 nullptr 表示未注册） */
    const MetaClass* GetClass(const std::type_index& typeIndex);

    /** 便捷模板：通过组件类型获取 MetaClass */
    template<typename T>
    const MetaClass* GetClassFor() {
        return GetClass(std::type_index(typeid(T)));
    }

    /** 便捷模板：通过组件指针获取 MetaClass */
    template<typename T>
    const MetaClass* GetClassFor(const T& obj) {
        return GetClass(std::type_index(typeid(obj)));
    }

} // namespace Reflect
} // namespace Engine

// ═══════════════════════════════════════════════════════════════
// 便捷注册宏
//
// 在组件 .cpp 文件末尾使用：
//   REFLECT_REGISTER(PointLightComponent);
// ═══════════════════════════════════════════════════════════════
#define REFLECT_REGISTER(ClassName) \
    static bool s_ReflectReg_##ClassName = []() { \
        ClassName::RegisterReflection(); \
        return true; \
    }()