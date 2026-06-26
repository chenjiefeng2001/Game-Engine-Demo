#pragma once

/**
 * @file OpenGLComputeParticles.h
 * @brief GPU 粒子系统 — 基于 OpenGL Compute Shader + SSBO
 *
 * 类似 Unity VFX Graph / Unreal Niagara 的 GPU 驱动粒子系统，
 * 支持百万级粒子模拟。所有粒子更新在 GPU 上完成，CPU 仅负责
 * Dispatch 和发射新粒子。
 *
 * 架构：
 *   CPU: Emitter (每帧发射) → SSBO (粒子数据) → Compute Shader (更新)
 *                                                      ↓
 *                                         DispatchIndirect / Instanced Rendering
 */

#include "Engine/Types.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Editor/VFXGraph/VFXGraphCore.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>

struct GladGLContext;

namespace Engine {

    class Shader;
    class VertexArray;

    class OpenGLComputeParticles : public VFX::VFXInstance {
    public:
        OpenGLComputeParticles(GladGLContext& gl);
        virtual ~OpenGLComputeParticles();

        /// 初始化 GPU 资源（SSBO + Shader）
        bool Init();

        /// 重设系统（清空所有粒子）
        void Clear();

        /// 每帧更新（Dispatch compute shader）
        void OnUpdate(float dt);

        /// 渲染粒子（Instanced billboard draw）
        void Render(const glm::mat4& view, const glm::mat4& proj);

        /// 手动发射 burst 粒子
        void Burst(uint32 count);

        /// 获取当前活跃粒子数
        uint32 GetAliveCount() const { return m_AliveCount; }
        uint32 GetCapacity() const { return m_Capacity; }

        /// 获取 GPU 粒子 SSBO handle
        uint32 GetParticleSSBO() const { return m_ParticleSSBO; }

        /// 获取计数 SSBO handle（用于 indirect dispatch）
        uint32 GetCountSSBO() const { return m_CountSSBO; }

    private:
        // ── GPU 资源 ──
        GladGLContext& m_GL;
        uint32 m_ParticleSSBO = 0;     // 粒子数据 SSBO
        uint32 m_CountSSBO = 0;        // 粒子计数 SSBO (alive count)
        uint32 m_AtomicSSBO = 0;       // 原子计数器 SSBO
        uint32 m_VAO = 0;              // Billboard quad VAO
        uint32 m_VBO = 0;              // Billboard quad VBO

        // ── Shader 程序 ──
        uint32 m_UpdateProgram = 0;    // 更新 compute shader
        uint32 m_RenderProgram = 0;    // 渲染 shader (vertex + fragment)

        // ── Uniform 位置 ──
        int32 m_DeltaTimeLoc = -1;
        int32 m_EmitterPosLoc = -1;
        int32 m_EmitRateLoc = -1;
        int32 m_ViewProjLoc = -1;
        int32 m_TimeLoc = -1;

        // ── 配置 ──
        VFX::EmitterConfig m_Config;
        uint32 m_Capacity = VFX::kDefaultParticleCapacity;
        uint32 m_AliveCount = 0;
        float m_EmitAccumulator = 0.0f;
        float m_TotalTime = 0.0f;
        bool m_Initialized = false;

        // ── 内部 ──
        bool CreateShaders();
        bool CreateBuffers();
        bool CreateBillboardQuad();
        void UploadEmitData(uint32 count, const glm::vec3& position);
    };

} // namespace Engine