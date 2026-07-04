#pragma once

/**
 * @file MaterialInstance.h
 * @brief 材质实例 — 基于 Dynamic UBO 的高效材质参数切换系统
 *
 * 设计目标：
 *   消除每物体每帧的独立 UBO 上传 API 调用。
 *   通过 Dynamic UBO + 偏移切换，只需一次 Descriptor Set 绑定即可渲染所有材质。
 *
 * 核心架构：
 * ```
 * MasterMaterial (着色器模板 + 宏变体编译 + 反射数据)
 *       │
 *       ▼ creates
 * MaterialInstance (材质参数集 + 纹理绑定)
 *       │
 *       ├── SetParameter("BaseColor", vec4) → 写入 CPU 镜像 (m_ParameterData)
 *       ├── SetTexture("AlbedoMap", tex)    → 绑定纹理槽
 *       │
 *       ▼ 每帧绘制
 * Renderer::Submit(mesh, materialInstance)
 *       │
 *       ├── materialInstance->Apply(cmd, allocator)
 *       │     ├── 从 DynamicUBOAllocator 分配空间
 *       │     ├── memcpy(m_ParameterData → mappedPtr + offset)
 *       │     ├── cmd.SetConstantBuffer(set, binding, buffer, offset)
 *       │     └── cmd.SetShaderResource(set, binding, texture)
 *       │
 *       └── cmd.Draw(mesh)
 * ```
 *
 * 使用方式：
 * @code
 *   auto master = MasterMaterial::Registry::Get().Find(SID("StandardPBR"));
 *   MaterialInstance instance(master);
 *
 *   instance.SetParameter("BaseColor", {1.0f, 0.8f, 0.3f, 1.0f});
 *   instance.SetTexture("AlbedoMap", albedoTexture);
 *
 *   // 每帧渲染
 *   instance.Apply(cmd, uboAllocator);
 *   cmd.DrawIndexed(mesh.GetIndexCount());
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Rendering/MasterMaterial.h"
#include "Engine/Rendering/ShaderReflection.h"
#include "Engine/Rendering/AutoPipelineLayout.h"
#include "Engine/Rendering/DynamicUBOAllocator.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstring>

namespace Engine {

    // 前向声明
    class Texture;

    namespace Rendering {

        // ============================================================
        // 材质参数类型枚举
        // ============================================================
        enum class MaterialParamType : uint8 {
            Float   = 0,
            Float2  = 1,
            Float3  = 2,
            Float4  = 3,
            Int     = 4,
            Texture = 5,
        };

        // ============================================================
        // 参数元数据 — 由反射系统自动填充
        // ============================================================
        struct MaterialParamMeta {
            StringID         name;
            MaterialParamType type    = MaterialParamType::Float;
            uint32_t         offset  = 0;       ///< 在 m_ParameterData 中的字节偏移
            uint32_t         size    = 0;       ///< 数据大小
            uint32_t         binding = 0;       ///< 纹理绑定点（仅对 Texture 类型）
            uint32_t         set     = 0;       ///< Descriptor set（仅对 Texture 类型）
        };

        // ============================================================
        // 纹理槽位绑定数据
        // ============================================================
        struct MaterialTextureBinding {
            uint32_t              set     = 0;
            uint32_t              binding = 0;
            std::shared_ptr<Texture> texture;   ///< 纹理（不能是 IRHITexture*，因为 Texture 是上层资源类型）
        };

        // ============================================================
        // MaterialInstance — 材质实例
        // ============================================================
        /**
         * @brief 材质实例 — 持有具体的材质参数值和纹理引用
         *
         * 非线程安全（应在渲染线程使用）。
         * 每个实例对应一个 MasterMaterial + 一组参数值。
         */
        class MaterialInstance {
        public:
            /**
             * @brief 构造材质实例
             *
             * @param master 材质模板（引用计数保持）
             * @param name   可选名称（调试用）
             */
            explicit MaterialInstance(std::shared_ptr<MasterMaterial> master,
                                       std::string name = "UnnamedMaterial");

            ~MaterialInstance() = default;

            // 禁止拷贝（参数数据可能很大）
            MaterialInstance(const MaterialInstance&) = delete;
            MaterialInstance& operator=(const MaterialInstance&) = delete;

            // 允许移动
            MaterialInstance(MaterialInstance&&) noexcept = default;
            MaterialInstance& operator=(MaterialInstance&&) noexcept = default;

            // ════════════════════════════════════════════════════════
            // 参数设置
            // ════════════════════════════════════════════════════════

            /** @brief 设置 float 参数 */
            void SetParameter(StringID name, float value);

            /** @brief 设置 vec2 参数（2 个 float） */
            void SetParameter2f(StringID name, const float* values);

            /** @brief 设置 vec3 参数（3 个 float） */
            void SetParameter3f(StringID name, const float* values);

            /** @brief 设置 vec4 参数（4 个 float） */
            void SetParameter4f(StringID name, const float* values);

            /** @brief 设置 int 参数 */
            void SetParameterInt(StringID name, int32_t value);

            /**
             * @brief 设置通用参数（从反射数据自动推导类型和偏移）
             *
             * @param name  参数名称
             * @param data  数据指针
             * @param size  数据大小（字节）
             * @return true 表示设置成功
             */
            bool SetParameterRaw(StringID name, const void* data, uint32_t size);

            /**
             * @brief 设置纹理绑定
             *
             * @param name  纹理槽位名称（如 "AlbedoMap"）
             * @param tex   纹理资源
             * @return true 表示绑定成功
             */
            bool SetTexture(StringID name, std::shared_ptr<Texture> tex);

            // ════════════════════════════════════════════════════════
            // GPU 提交
            // ════════════════════════════════════════════════════════

            /**
             * @brief 将材质参数提交到 GPU
             *
             * 此方法会：
             *   1. 从 DynamicUBOAllocator 分配空间
             *   2. memcpy 参数数据到映射指针
             *   3. 绑定 UBO 到命令列表（使用动态偏移）
             *   4. 绑定所有纹理到命令列表
             *
             * @param cmd        RHI 命令列表
             * @param allocator  动态 UBO 分配器
             * @return true 表示提交成功
             */
            bool Apply(RHI::IRHICommandList& cmd,
                       DynamicUBOAllocator& allocator);

            // ════════════════════════════════════════════════════════
            // 查询
            // ════════════════════════════════════════════════════════

            /** 获取关联的 Master Material */
            MasterMaterial* GetMaster() const noexcept { return m_Master.get(); }

            /** 获取实例名称 */
            const std::string& GetName() const noexcept { return m_Name; }

            /** 参数数据总大小（字节） */
            uint32_t GetParameterDataSize() const noexcept {
                return static_cast<uint32_t>(m_ParameterData.size());
            }

            /** 纹理绑定数量 */
            size_t GetTextureCount() const noexcept { return m_Textures.size(); }

            /** 是否有脏数据需要上传 */
            bool IsDirty() const noexcept { return m_IsDirty; }

            /** 手动标记脏（强制下次 Apply 上传所有数据） */
            void MarkDirty() noexcept { m_IsDirty = true; }

        private:
            // ── 材质模板 ──
            std::shared_ptr<MasterMaterial> m_Master;
            std::string m_Name;

            // ── 参数数据（CPU 镜像） ──
            std::vector<uint8_t> m_ParameterData;        ///< 参数数据的连续缓冲区
            bool                 m_IsDirty = true;       ///< 是否有未上传的更改

            // ── 参数布局（从反射初始化） ──
            std::unordered_map<uint64_t, MaterialParamMeta> m_ParamLayout;

            // ── 纹理绑定 ──
            std::vector<MaterialTextureBinding> m_Textures;

            // ── 上次分配的 UBO 偏移（用于调试/统计） ──
            uint64_t m_LastUBOOffset = 0;

            // ── 内部方法 ──

            /**
             * @brief 从 Master Material 的着色器反射初始化参数布局
             *
             * 此方法会：
             *   1. 获取 Master Material 对应变体的 ShaderReflectionData
             *   2. 遍历所有 UniformBlock 中的成员
             *   3. 构建 m_ParamLayout 映射表和 m_ParameterData 缓冲区
             */
            void BuildParameterLayout();

            /**
             * @brief 在布局中查找参数元数据
             *
             * @param name 参数名称的 StringID
             * @return 指向元数据的指针，未找到则返回 nullptr
             */
            const MaterialParamMeta* FindParamMeta(StringID name) const noexcept;
        };

    } // namespace Rendering
} // namespace Engine