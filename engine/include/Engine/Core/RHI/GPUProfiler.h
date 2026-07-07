#pragma once

/**
 * @file GPUProfiler.h
 * @brief GPU 性能分析器 — 自动 Timestamp 查询 + 调试标记注入
 *
 * 集成到 RenderGraph 后，每个 Pass 自动：
 *   1. 插入 vkCmdWriteTimestamp / EndQuery 标记开始/结束
 *   2. 帧结束自动读回时间结果
 *   3. 通过 ImGui 面板以条形图显示
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Engine {
namespace RHI {

    // ============================================================
    // GPUTimestamp — 单次时间戳查询结果
    // ============================================================
    struct GPUTimestamp {
        std::string name;
        float       timeMS = 0.0f;  ///< GPU 耗时（毫秒）
        bool        valid  = false;
    };

    // ============================================================
    // GPUProfiler — 高级 GPU 分析器
    // ============================================================
    class GPUProfiler {
    public:
        GPUProfiler() = default;
        ~GPUProfiler();

        GPUProfiler(const GPUProfiler&) = delete;
        GPUProfiler& operator=(const GPUProfiler&) = delete;

        /**
         * @brief 初始化（需要真正的 RHI 设备指针，用于创建 QueryPool）
         * 返回 true 表示支持 GPU 时间戳
         */
        bool Initialize();

        /** @brief 开始一个 GPU Pass 时间范围 */
        void BeginPass(const std::string& name);

        /** @brief 结束一个 GPU Pass 时间范围 */
        void EndPass();

        /**
         * @brief 帧结束 — 读回所有 Timestamp 结果
         * 必须在 GPU 完成所有提交后调用（即 WaitIdle 后）
         */
        void EndFrame();

        /**
         * @brief 获取指定 Pass 的 GPU 耗时（毫秒）
         */
        float GetPassTime(const std::string& name) const;

        /**
         * @brief 获取所有 Pass 的时间数据（用于 ImGui 可视化）
         */
        const std::vector<GPUTimestamp>& GetAllTimestamps() const noexcept {
            return m_Timestamps;
        }

        /** @brief 是否支持 GPU 时间戳 */
        bool IsSupported() const noexcept { return m_Supported; }

    private:
        bool m_Supported = false;
        std::vector<GPUTimestamp> m_Timestamps;
        std::unordered_map<std::string, size_t> m_TimestampMap;

        // 查询池相关（由具体后端实现）
        void* m_QueryPool = nullptr;  // VkQueryPool 或 ID3D12QueryHeap
        uint32_t m_QueryIndex = 0;
        static constexpr uint32_t kMaxQueries = 1024;
    };

} // namespace RHI
} // namespace Engine