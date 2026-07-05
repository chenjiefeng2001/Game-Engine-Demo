#pragma once

/**
 * @file MasterMaterial.h
 * @brief Master Material — 材质模板定义，宏变体编译，Shader 变体缓存
 *
 * 设计灵感：Unreal Engine 的 UMaterial 系统
 *
 * 核心概念：
 *   - Master Material 是一个着色器模板，定义了可选的宏开关
 *   - 每个 VariantKey（位掩码）对应一个编译后的 PSO
 *   - 材质实例只存储参数，不涉及编译
 *
 * 宏变体 (Variant Key) — 位掩码编码：
 *   bit 0: USE_NORMALMAP    — 是否使用法线贴图
 *   bit 1: USE_EMISSIVE     — 是否使用自发光
 *   bit 2: USE_AO           — 是否使用环境光遮蔽贴图
 *   bit 3: ALPHA_TEST       — Alpha 裁剪模式
 *   bit 4: USE_PARALLAX     — 视差贴图
 *   bit 5: SKINNED           — 骨骼蒙皮
 *
 * 使用方式：
 * @code
 *   auto* mm = MasterMaterial::GetRegistry().Find(SID("StandardPBR"));
 *   MasterMaterial::VariantKey key;
 *   key.flags = VariantKey::USE_NORMALMAP | VariantKey::USE_EMISSIVE;
 *   auto shader = mm->GetOrCompileShader(key);  // 按需编译 + 缓存
 * @endcode
 */

#include "Engine/Core/StringID.h"
#include "Engine/Core/RHI/PSODesc.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstdint>

namespace Engine {

    // 前向声明
    class IGraphicsFactory;
    class Shader;

    namespace Rendering {

        // ============================================================
        // 变体键 — 位掩码编码器
        // ============================================================
        struct VariantKey {
            uint32_t flags = 0;

            enum : uint32_t {
                USE_NORMALMAP    = 1 << 0,
                USE_EMISSIVE     = 1 << 1,
                USE_AO           = 1 << 2,
                ALPHA_TEST       = 1 << 3,
                USE_PARALLAX     = 1 << 4,
                SKINNED          = 1 << 5,
            };

            bool operator==(const VariantKey& o) const noexcept { return flags == o.flags; }
            bool operator!=(const VariantKey& o) const noexcept { return flags != o.flags; }
            bool Has(uint32_t bit) const noexcept { return (flags & bit) != 0; }
        };

        // ============================================================
        // 宏定义条目
        // ============================================================
        /**
         * @brief 单个宏定义描述（用于编译时注入 GLSL #define）
         */
        struct MacroDefinition {
            std::string name;           ///< 宏名称（如 "USE_NORMALMAP"）
            std::string value = "1";    ///< 宏值（通常为 "1"）
        };

        // ============================================================
        // Master Material 定义
        // ============================================================
        /**
         * @brief 材质模板 — 定义一个材质的所有输入槽 + 编译规则
         *
         * 每个 MasterMaterial 实例持有着色器源码模板，
         * 运行时按 VariantKey 编译对应的变体。
         */
        class MasterMaterial : public std::enable_shared_from_this<MasterMaterial> {
        public:
            // ── 构造 ──
            MasterMaterial(StringID id, std::string name, std::string domain = "Surface")
                : m_ID(id), m_Name(std::move(name)), m_Domain(std::move(domain)) {}

            // ── 着色器源码设置 ──

            /** @brief 设置顶点着色器源码模板 */
            void SetVertexSource(std::string source) { m_VertexSrc = std::move(source); }

            /** @brief 设置片段着色器源码模板 */
            void SetFragmentSource(std::string source) { m_FragmentSrc = std::move(source); }

            // ── 变体编译 ──

            /**
             * @brief 根据变体键获取或编译着色器
             *
             * @param key    变体键（位掩码）
             * @param factory 图形工厂（用于创建 Shader）
             * @return 编译后的 Shader 指针（已缓存）
             *
             * 线程安全。首次编译后会缓存到 m_VariantCache。
             */
            std::shared_ptr<Shader> GetOrCompileShader(
                const VariantKey& key,
                IGraphicsFactory& factory);

            // ── 变体查询 ──

            /** @brief 获取变体键对应的宏定义列表 */
            std::vector<MacroDefinition> GetMacros(const VariantKey& key) const;

            /** @brief 从宏定义生成 GLSL #define 前缀 */
            std::string GenerateShaderPreamble(const VariantKey& key) const;

            // ── 全局注册表 ──

            /**
             * @brief 全局 Master Material 注册表
             */
            class Registry {
            public:
                static Registry& Get() { static Registry s; return s; }

                void Register(std::shared_ptr<MasterMaterial> mm);
                MasterMaterial* Find(StringID id);
                size_t Count() const noexcept { return m_Materials.size(); }

            private:
                std::unordered_map<uint64_t, std::shared_ptr<MasterMaterial>> m_Materials;
                std::mutex m_Mutex;
            };

            // ── 访问器 ──
            StringID GetID() const noexcept { return m_ID; }
            const std::string& GetName() const noexcept { return m_Name; }
            const std::string& GetDomain() const noexcept { return m_Domain; }

        private:
            StringID m_ID;              ///< 唯一标识符
            std::string m_Name;         ///< 显示名称
            std::string m_Domain;       ///< "Surface" / "Decal" / "PostProcess"
            std::string m_VertexSrc;    ///< 顶点着色器源码模板
            std::string m_FragmentSrc;  ///< 片段着色器源码模板

            // ── 变体缓存 ──
            std::mutex m_VariantMutex;
            std::unordered_map<uint32_t, std::shared_ptr<Shader>> m_VariantCache;
        };

    } // namespace Rendering
} // namespace Engine

// ── std::hash 特化（支持 VariantKey 作为 unordered_map 键） ──
namespace std {
    template <>
    struct hash<Engine::Rendering::VariantKey> {
        size_t operator()(const Engine::Rendering::VariantKey& k) const noexcept {
            return static_cast<size_t>(k.flags);
        }
    };
}