#pragma once

/**
 * @file PerformanceWindow.h
 * @brief 性能监控窗口 — 综合图形调试面板
 *
 * 包含以下组别：
 *   1. FPS 计数器 / 帧时间 (Frame Time): CPU & GPU 耗时
 *   2. Draw Calls 统计
 *   3. 三角形/顶点计数
 *   4. 显存占用 (VRAM Usage)
 *   5. GPU Profiler 视图: 实时瀑布图
 *
 * 每帧调用 FeedStats() 传入统计数据，
 * 在 Application::OnImGui() 中调用 OnImGui() 即可显示。
 */

#include "Engine/Types.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/BufferVisualization.h"
#include <string>
#include <array>

namespace Engine {

    /// GPU Pass 时间片数据
    struct GPUPassData {
        std::string name;
        float       timeMs = 0.0f;
    };

    /// 单帧 GPU Profiler 快照
    struct GPUProfilerSnapshot {
        static constexpr uint32 kMaxPasses = 32;
        GPUPassData passes[kMaxPasses];
        uint32      passCount = 0;
    };

    class PerformanceWindow {
    public:
        PerformanceWindow();
        ~PerformanceWindow() = default;

        // 禁止拷贝
        PerformanceWindow(const PerformanceWindow&) = delete;
        PerformanceWindow& operator=(const PerformanceWindow&) = delete;

        // ── 帧统计输入 ──

        /** 每帧传入最新统计数据 */
        void FeedStats(float32 frameTimeMs,
                       uint32 drawCallCount,
                       uint32 physicsBodyCount,
                       uint32 physicsJointCount,
                       uint32 activeAudioSources,
                       uint32 textureCount);

        /** 设置 CPU 耗时（应用逻辑耗时，排除等待） */
        void SetCPUTime(float32 ms) { m_CPUTimeMs = ms; }

        /** 设置 GPU 耗时（从 GPU 时间戳查询获得） */
        void SetGPUTime(float32 ms) { m_GPUTimeMs = ms; }

        /** 设置三角形/顶点计数 */
        void SetGeometryCount(uint32 vertexCount, uint32 triangleCount) {
            m_VertexCount   = vertexCount;
            m_TriangleCount = triangleCount;
        }

        /** 设置显存占用（字节） */
        void SetVRAMUsage(uint64 texturesBytes,
                          uint64 buffersBytes,
                          uint64 modelsBytes) {
            m_VRAMTextureBytes = texturesBytes;
            m_VRAMBufferBytes  = buffersBytes;
            m_VRAMModelBytes   = modelsBytes;
        }

        /** 设置 GPU Profiler 快照 */
        void SetGPUProfiler(const GPUProfilerSnapshot& snap) {
            m_GPUProfiler = snap;
        }

        /** 设置渲染上下文指针（用于 view mode 切换） */
        void SetRenderContext(class IRenderContext* ctx) { m_RenderContext = ctx; }

        // ── 渲染 ──

        /** 在 OnImGui() 中调用，绘制性能窗口 */
        void OnImGui();

        // ── 配置 ──

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const        { return m_Visible; }
        void ToggleVisibility()       { m_Visible = !m_Visible; }

    private:
        // ── 辅助绘制方法 ──
        void DrawFPSFrameTime();
        void DrawDrawCalls();
        void DrawGeometryCounts();
        void DrawVRAMUsage();
        void DrawGPUProfiler();
        void DrawViewModes();
        void DrawBufferVisualization();
        void DrawLightingAndShadows();
        void DrawGeometryAndCulling();
        void DrawPostProcessing();
        void DrawTexturesAndAssets();
        void DrawHelperToggles();

        // ── FPS 历史环形缓冲区（用于折线图） ──
        static constexpr uint32 kHistorySize = 150;
        float32 m_FpsHistory[kHistorySize] = {};
        uint32  m_HistoryIndex = 0;
        uint32  m_HistoryCount = 0;

        // ── Frame Time 历史 ──
        float32 m_FrameTimeHistory[kHistorySize] = {};

        // ── 当前帧统计 ──
        float32 m_CurrentFPS        = 0.0f;
        float32 m_FrameTimeMs       = 0.0f;
        float32 m_CPUTimeMs         = 0.0f; // CPU 逻辑耗时
        float32 m_GPUTimeMs         = 0.0f; // GPU 渲染耗时
        uint32  m_DrawCallCount     = 0;
        uint32  m_PhysicsBodyCount  = 0;
        uint32  m_PhysicsJointCount = 0;
        uint32  m_ActiveAudioSources = 0;
        uint32  m_TextureCount      = 0;

        // ── 几何体统计 ──
        uint32  m_VertexCount       = 0;
        uint32  m_TriangleCount     = 0;

        // ── 显存占用 ──
        uint64  m_VRAMTextureBytes  = 0;
        uint64  m_VRAMBufferBytes   = 0;
        uint64  m_VRAMModelBytes    = 0;

        // ── GPU Profiler ──
        GPUProfilerSnapshot m_GPUProfiler;

        // ── 缓冲区可视化帧数据缓存 ──
        BufferVisFrameData m_BufferVisData;
        BufferVisMode m_BufferVisSelectedMode = BufferVisMode::Depth;
        std::vector<float> m_CachedDepthBuffer;
        int32 m_CachedDepthW = 0, m_CachedDepthH = 0;
        bool m_DepthCacheValid = false;

        // ── 渲染上下文指针（用于 view mode 切换，非拥有） ──
        class IRenderContext* m_RenderContext = nullptr;

        // ── 窗口状态 ──
        bool m_Visible = true;
    };

} // namespace Engine