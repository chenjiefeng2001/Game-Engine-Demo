/**
 * @file GPUProfiler.cpp
 * @brief GPU 性能分析器实现 — 时间戳查询 + 调试标记
 *
 * 当前为占位实现（接口已就绪），真正的后端时间戳查询需要
 * Vulkan VK_EXT_host_query_reset / D3D12 ID3D12QueryHeap 绑定。
 * 
 * 使用方式：
 *   在 RenderGraph::Execute() 中自动调用：
 *     profiler.BeginPass("GBuffer");
 *     // ... pass execution ...
 *     profiler.EndPass();
 *     profiler.EndFrame(); // 帧结束
 */

#include "Engine/Core/RHI/GPUProfiler.h"

namespace Engine { namespace RHI {

GPUProfiler::~GPUProfiler() {
    // TODO: 释放 VkQueryPool 或 ID3D12QueryHeap
}

bool GPUProfiler::Initialize() {
    m_Supported = true; // 标记支持（真正后端需要创建 QueryPool）
    m_Timestamps.clear();
    m_TimestampMap.clear();
    m_QueryIndex = 0;
    return true;
}

void GPUProfiler::BeginPass(const std::string& name) {
    if (!m_Supported) return;

    GPUTimestamp ts;
    ts.name = name;
    ts.valid = false;
    ts.timeMS = 0.0f;

    auto it = m_TimestampMap.find(name);
    if (it != m_TimestampMap.end()) {
        m_Timestamps[it->second] = ts;
    } else {
        m_TimestampMap[name] = m_Timestamps.size();
        m_Timestamps.push_back(ts);
    }

    // TODO: vkCmdWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, queryIndex++)
    m_QueryIndex += 2; // 起始 + 结束 各一个
}

void GPUProfiler::EndPass() {
    if (!m_Supported) return;
    // TODO: vkCmdWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, queryIndex++)
    // 或 D3D12: commandList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex++)
}

void GPUProfiler::EndFrame() {
    if (!m_Supported) return;

    // TODO: 从 GPU 读回时间戳结果并计算差值
    // Vulkan: vkGetQueryPoolResults(device, queryPool, 0, queryCount, ...)
    // D3D12: commandList->ResolveQueryData(...) → Readback buffer → CPU 映射
    //
    // 占位：对所有有效的 timestamp，填充模拟数据
    for (auto& ts : m_Timestamps) {
        if (!ts.valid) {
            ts.timeMS = 0.5f; // 占位值
            ts.valid = true;
        }
    }

    m_QueryIndex = 0;
}

float GPUProfiler::GetPassTime(const std::string& name) const {
    auto it = m_TimestampMap.find(name);
    if (it != m_TimestampMap.end() && it->second < m_Timestamps.size()) {
        return m_Timestamps[it->second].timeMS;
    }
    return 0.0f;
}

}} // namespace Engine::RHI