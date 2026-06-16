#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>

namespace {
    Engine::Logger s_Log("SpatialAudio");
}

namespace Engine {
namespace Audio {

// ============================================================================
// 构造函数与公共接口
// ============================================================================

SpatialAudioManager::SpatialAudioManager()
{
    // 初始化默认参数
    m_DiffractionParams = DiffractionParams{};
    m_ReflectionParams = ReflectionParams{};
    m_OcclusionParams = ObstructionParams{};
}

void SpatialAudioManager::RegisterSource(AudioSourceHandle handle, const glm::vec3& position)
{
    // 移除已存在同 handle 的条目
    auto it = std::find_if(m_Sources.begin(), m_Sources.end(),
        [handle](const SpatialSourceData& d) { return d.handle == handle; });
    if (it != m_Sources.end())
    {
        it->position = position;
        return;
    }

    m_Sources.push_back({ position, handle });
}

void SpatialAudioManager::UnregisterSource(AudioSourceHandle handle)
{
    std::erase_if(m_Sources, [handle](const SpatialSourceData& d) { return d.handle == handle; });
}

void SpatialAudioManager::UpdateSourcePosition(AudioSourceHandle handle, const glm::vec3& position)
{
    for (auto& source : m_Sources)
    {
        if (source.handle == handle)
        {
            source.position = position;
            break;
        }
    }
}

void SpatialAudioManager::SetRaycastCallback(RaycastCallback callback)
{
    m_Raycast = std::move(callback);
}

void SpatialAudioManager::SetSceneGeometry(const std::vector<GeometryTriangle>& triangles)
{
    m_Geometry = triangles;
}

void SpatialAudioManager::ClearSceneGeometry()
{
    m_Geometry.clear();
}

void SpatialAudioManager::SetDiffractionParams(const DiffractionParams& params)
{
    m_DiffractionParams = params;
}

void SpatialAudioManager::SetReflectionParams(const ReflectionParams& params)
{
    m_ReflectionParams = params;
}

void SpatialAudioManager::SetOcclusionParams(const ObstructionParams& params)
{
    m_OcclusionParams = params;
}

void SpatialAudioManager::RegisterIRCallback(IRCallback callback)
{
    m_IRCallback = std::move(callback);
}

// ============================================================================
// 核心：多路径 IR 合成
// ============================================================================

std::vector<SpatialIR> SpatialAudioManager::ComputeSpatialIRs(
    const glm::vec3& sourcePos,
    const glm::vec3& listenerPos,
    const glm::vec3& listenerForward,
    const glm::vec3& listenerUp) const
{
    std::vector<SpatialIR> results;

    // 1. 直达路径
    SpatialIR direct = ComputeDirectPath(sourcePos, listenerPos);
    ApplyLTIPropertyToIR(direct, sourcePos, listenerPos);
    results.push_back(std::move(direct));

    // 2. 反射路径
    auto reflections = ComputeReflections(sourcePos, listenerPos, listenerForward, listenerUp);
    for (auto& ir : reflections)
    {
        ApplyLTIPropertyToIR(ir, sourcePos, listenerPos);
    }
    results.insert(results.end(),
        std::make_move_iterator(reflections.begin()),
        std::make_move_iterator(reflections.end()));

    // 3. 衍射路径
    SpatialIR diffraction = ComputeDiffraction(sourcePos, listenerPos, listenerForward, listenerUp);
    if (diffraction.gain > 0.001f)
    {
        ApplyLTIPropertyToIR(diffraction, sourcePos, listenerPos);
        results.push_back(std::move(diffraction));
    }

    // 触发 IR 回调
    if (m_IRCallback)
    {
        for (const auto& ir : results)
        {
            m_IRCallback(ir);
        }
    }

    return results;
}

// ============================================================================
// 直达路径计算
// ============================================================================

SpatialIR SpatialAudioManager::ComputeDirectPath(
    const glm::vec3& source,
    const glm::vec3& listener) const
{
    SpatialIR ir;
    ir.type = SpatialPathType::Direct;

    glm::vec3 dir = listener - source;
    float distance = glm::length(dir);
    if (distance < 0.001f)
    {
        ir.gain = 1.0f;
        ir.delay = 0.0f;
        return ir;
    }

    // 传播延迟
    float speedOfSound = 343.3f; // 20°C 声速 m/s
    ir.delay = distance / speedOfSound;

    // 距离衰减（1/r 模型）
    ir.gain = 1.0f / (distance + 1.0f);

    // 遮挡/阻断/排他计算
    ir.occlusion = ComputeOcclusion(source, listener);

    // 应用遮挡对增益的影响
    ApplyOcclusionToIR(ir, ir.occlusion);

    return ir;
}

// ============================================================================
// 反射路径计算（镜像声源法）
// ============================================================================

std::vector<SpatialIR> SpatialAudioManager::ComputeReflections(
    const glm::vec3& source,
    const glm::vec3& listener,
    const glm::vec3& forward,
    const glm::vec3& up) const
{
    std::vector<SpatialIR> results;
    if (m_Geometry.empty())
    {
        // 无几何数据时，简化使用水平面 (Y=0) 作为反射面
        float floorY = 0.0f;
        if ((source.y > floorY && listener.y > floorY) ||
            (source.y < floorY && listener.y < floorY))
        {
            // 声源和听者在同一侧
            SpatialIR ir;
            ir.type = SpatialPathType::Reflection;

            // 镜像声源
            glm::vec3 mirrorSrc = source;
            mirrorSrc.y = floorY - (source.y - floorY);

            glm::vec3 dir = listener - mirrorSrc;
            float dist = glm::length(dir);

            ir.delay = dist / 343.3f;
            ir.gain = 0.7f / (dist + 1.0f); // 反射有能量损失
            ir.occlusion.occlusionFactor = 0.1f;

            // 为反射路径生成简短的脉冲响应系数
            ir.coefficients = { 0.7f, 0.3f, 0.1f, 0.05f };

            s_Log.Trace("Reflection path (floor mirror) delay={:.3f}s gain={:.3f}", ir.delay, ir.gain);
            results.push_back(std::move(ir));
        }
        return results;
    }

    // 有几何数据时：对每个三角面计算镜像
    // 只处理 1 阶反射（m_ReflectionParams.maxReflectionOrder = 1 时）
    uint32_t maxOrder = std::min(m_ReflectionParams.maxReflectionOrder, 1u);
    if (maxOrder < 1) return results;

    for (const auto& tri : m_Geometry)
    {
        // 计算三角面法线（平均法线）
        glm::vec3 normal = glm::normalize(
            (tri.normals[0] + tri.normals[1] + tri.normals[2]) / 3.0f);

        // 三角面中心
        glm::vec3 center = (tri.vertices[0] + tri.vertices[1] + tri.vertices[2]) / 3.0f;

        // 检查声源是否在面的"正面"（法线指向的一侧）
        glm::vec3 srcToCenter = center - source;
        float dotToSrc = glm::dot(normal, srcToCenter);
        if (dotToSrc < 0.0f) continue; // 声源在背面，无反射

        // 镜像声源
        float distToPlane = glm::dot(normal, source - center);
        glm::vec3 mirrorSrc = source - 2.0f * distToPlane * normal;

        // 检查镜像是否在面的正面
        glm::vec3 mirrorToCenter = center - mirrorSrc;
        if (glm::dot(normal, mirrorToCenter) < 0.0f) continue;

        // 计算反射路径
        glm::vec3 reflectDir = listener - mirrorSrc;
        float reflectDist = glm::length(reflectDir);
        if (reflectDist < 0.001f) continue;

        SpatialIR ir;
        ir.type = SpatialPathType::Reflection;
        ir.delay = reflectDist / 343.3f;
        ir.gain = (1.0f - tri.absorption) * m_ReflectionParams.reflectionGainScale / (reflectDist + 1.0f);

        // 散射效果：在脉冲响应中添加散射尾部
        size_t irLen = std::max(static_cast<size_t>(m_ReflectionParams.rirLength * 100.0f), size_t(4));
        ir.coefficients.resize(irLen, 0.0f);
        ir.coefficients[0] = ir.gain;
        for (size_t i = 1; i < irLen; ++i)
        {
            ir.coefficients[i] = ir.gain * tri.scattering * std::exp(-2.0f * static_cast<float>(i) / static_cast<float>(irLen));
        }

        results.push_back(std::move(ir));
    }

    return results;
}

// ============================================================================
// 衍射计算 (UTD - Uniform Theory of Diffraction)
// ============================================================================

SpatialIR SpatialAudioManager::ComputeDiffraction(
    const glm::vec3& source,
    const glm::vec3& listener,
    const glm::vec3& /*forward*/,
    const glm::vec3& /*up*/) const
{
    SpatialIR ir;
    ir.type = SpatialPathType::Diffraction;
    ir.diffractionAmount = 0.0f;
    ir.diffractionAngle = 0.0f;
    ir.gain = 0.0f;

    if (!m_DiffractionParams.enableUTDDiffraction || m_Geometry.empty())
    {
        return ir;
    }

    // 遍历所有几何边寻找可能的衍射路径
    float maxDiffractionGain = 0.0f;
    glm::vec3 bestEdgeStart, bestEdgeEnd;

    for (size_t t = 0; t < m_Geometry.size(); ++t)
    {
        const auto& tri = m_Geometry[t];
        for (int e = 0; e < 3; ++e)
        {
            glm::vec3 v0 = tri.vertices[e];
            glm::vec3 v1 = tri.vertices[(e + 1) % 3];

            // 计算源-边-听者的衍射
            float diffGain = ComputeUTDDiffraction(source, listener, v0, v1);
            if (diffGain > maxDiffractionGain)
            {
                maxDiffractionGain = diffGain;
                bestEdgeStart = v0;
                bestEdgeEnd = v1;
            }
        }
    }

    if (maxDiffractionGain > 0.001f)
    {
        ir.gain = maxDiffractionGain * m_DiffractionParams.diffractionStrength;
        ir.diffractionAmount = ir.gain;

        // 计算衍射角
        glm::vec3 edgeMid = (bestEdgeStart + bestEdgeEnd) / 2.0f;
        glm::vec3 srcToEdge = glm::normalize(edgeMid - source);
        glm::vec3 edgeToListener = glm::normalize(listener - edgeMid);
        ir.diffractionAngle = std::acos(
            glm::clamp(glm::dot(srcToEdge, edgeToListener), -1.0f, 1.0f));

        // 传播延迟（通过衍射点的路径）
        float dist1 = glm::length(edgeMid - source);
        float dist2 = glm::length(listener - edgeMid);
        ir.delay = (dist1 + dist2) / 343.3f;
        ir.diffractionAmount = ir.gain;

        // 衍射的脉冲响应：低通特性
        ir.coefficients = { ir.gain, ir.gain * 0.5f, ir.gain * 0.2f };
    }

    return ir;
}

// ============================================================================
// UTD 衍射计算
// ============================================================================

float SpatialAudioManager::ComputeUTDDiffraction(
    const glm::vec3& source,
    const glm::vec3& listener,
    const glm::vec3& edgeStart,
    const glm::vec3& edgeEnd) const
{
    // 找到边缘上离源和听者最近的点
    glm::vec3 edgeDir = glm::normalize(edgeEnd - edgeStart);
    float edgeLen = glm::length(edgeEnd - edgeStart);

    // 参数化边缘
    auto closestPointOnEdge = [&](const glm::vec3& pt) -> glm::vec3
    {
        float t = glm::dot(pt - edgeStart, edgeDir);
        t = glm::clamp(t, 0.0f, edgeLen);
        return edgeStart + t * edgeDir;
    };

    glm::vec3 srcProj = closestPointOnEdge(source);
    glm::vec3 lstProj = closestPointOnEdge(listener);

    // 如果源和听者在同一投影点，无衍射
    float projDist = glm::distance(srcProj, lstProj);
    if (projDist < 0.01f) return 0.0f;

    // 源到边的距离
    float dist1 = glm::distance(source, srcProj);
    float dist2 = glm::distance(listener, lstProj);
    if (dist1 < 0.001f || dist2 < 0.001f) return 0.0f;

    // 计算入射角和衍射角
    glm::vec3 edgeAxis = glm::normalize(lstProj - srcProj);
    glm::vec3 srcToEdge = glm::normalize(srcProj - source);
    glm::vec3 edgeToListener = glm::normalize(listener - lstProj);

    // 衍射角（源-边-听者夹角）
    float cosTheta = glm::clamp(glm::dot(srcToEdge, edgeToListener), -1.0f, 1.0f);
    float theta = std::acos(cosTheta);

    // 如果衍射角超过最大值
    if (theta > m_DiffractionParams.maxDiffractionAngle)
    {
        return 0.0f;
    }

    // UTD 衍射系数简化计算
    // 实际 UTD 需要更复杂的计算（包括边缘绕射系数、距离因子等）
    // 这里使用简化模型：
    //   gain = 1 / (dist1 * dist2) * (sin(theta) + epsilon)
    float sinTheta = std::sin(theta);
    float gain = sinTheta / ((dist1 + dist2) * (1.0f + theta));

    // 频率相关衰减（几何衰减因子）
    float k = 2.0f * 3.14159265f * 500.0f / 343.3f; // 500Hz 参考
    float ka = k * projDist;
    float distanceFactor = 1.0f / std::sqrt(1.0f + ka * ka);

    gain *= distanceFactor;

    // 如果是衍射遮挡模式，额外衰减
    if (m_DiffractionParams.enableUTDDiffraction && m_OcclusionParams.enableDiffractionOcclusion)
    {
        gain *= (1.0f - std::min(theta / m_DiffractionParams.maxDiffractionAngle, 1.0f) * 0.5f);
    }

    return std::max(0.0f, gain);
}

// ============================================================================
// 遮挡/阻断/排他计算
// ============================================================================

OcclusionData SpatialAudioManager::ComputeOcclusion(
    const glm::vec3& source,
    const glm::vec3& listener) const
{
    OcclusionData occlusion;

    if (!m_Raycast)
    {
        // 无射线检测回调时，使用默认值（无遮挡）
        return occlusion;
    }

    // 射线采样：使用多次射线采样来提高遮挡检测的精确度
    const int numRays = 5;
    int hitCount = 0;
    float totalDistance = glm::distance(source, listener);
    if (totalDistance < 0.001f) return occlusion;

    glm::vec3 dir = (listener - source) / totalDistance;

    for (int i = 0; i < numRays; ++i)
    {
        // 在源-听者方向周围添加小偏移（圆锥采样）
        float spread = 0.1f; // 10° 散布
        glm::vec3 offset(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * spread
        );

        glm::vec3 rayEnd = listener;
        glm::vec3 rayStart = source + offset * totalDistance * 0.1f;

        if (m_Raycast(rayStart, rayEnd))
        {
            hitCount++;
        }
    }

    float hitRatio = static_cast<float>(hitCount) / static_cast<float>(numRays);

    // 遮挡：部分命中
    occlusion.occlusionFactor = hitRatio * 0.8f;

    // 阻断：连续命中
    if (hitCount >= numRays - 1)
    {
        occlusion.obstructionFactor = 0.9f;
    }
    else
    {
        occlusion.obstructionFactor = hitRatio * 0.6f;
    }

    // 排他：完全遮挡时
    if (hitRatio > m_OcclusionParams.exclusionThreshold)
    {
        occlusion.exclusionFactor = (hitRatio - m_OcclusionParams.exclusionThreshold) /
            (1.0f - m_OcclusionParams.exclusionThreshold);
    }

    return occlusion;
}

// ============================================================================
// LTI 系统辅助
// ============================================================================

void SpatialAudioManager::ApplyLTIPropertyToIR(
    SpatialIR& ir,
    const glm::vec3& source,
    const glm::vec3& listener) const
{
    // LTI 属性：
    // 1. 根据路径类型生成对应的脉冲响应系数（如果尚未设置）
    // 2. 应用传播延迟产生的相位偏移

    if (ir.coefficients.empty())
    {
        switch (ir.type)
        {
        case SpatialPathType::Direct:
            // 直达路径：短脉冲
            ir.coefficients = { ir.gain };
            break;

        case SpatialPathType::Reflection:
            // 反射路径：衰减尾部
            {
                size_t len = 8;
                ir.coefficients.resize(len);
                for (size_t i = 0; i < len; ++i)
                {
                    ir.coefficients[i] = ir.gain * std::exp(-2.0f * static_cast<float>(i));
                }
            }
            break;

        case SpatialPathType::Diffraction:
            // 衍射路径：低通特性
            {
                float cutoffFactor = m_DiffractionParams.lowpassCutoff / 44100.0f;
                ir.coefficients = { ir.gain * cutoffFactor, ir.gain * cutoffFactor * 0.3f };
            }
            break;
        }
    }

    // 应用距离对 LTI 系数的影响（距离越远，高频衰减越大）
    float dist = glm::distance(source, listener);
    float airAbsorption = std::exp(-0.0001f * dist);
    for (auto& c : ir.coefficients)
    {
        c *= airAbsorption;
    }
}

void SpatialAudioManager::ApplyOcclusionToIR(
    SpatialIR& ir,
    const OcclusionData& occlusion) const
{
    // 遮挡效果：应用低通滤波系数
    float occlusionFactor = occlusion.occlusionFactor;
    float obstructionFactor = occlusion.obstructionFactor;
    float exclusionFactor = occlusion.exclusionFactor;

    // 排他：完全静音
    if (exclusionFactor > 0.5f)
    {
        ir.gain *= (1.0f - exclusionFactor);
        for (auto& c : ir.coefficients)
        {
            c *= (1.0f - exclusionFactor);
        }
        return;
    }

    // 遮挡/阻断：低通滤波 + 增益衰减
    float combinedFactor = std::max(occlusionFactor, obstructionFactor);
    if (combinedFactor > 0.01f)
    {
        // 增益衰减
        float gainScale = 1.0f - combinedFactor * 0.7f;
        ir.gain *= gainScale;

        // 低通效果：修改脉冲响应系数
        float lowpassStrength = combinedFactor * 0.5f;
        for (size_t i = 0; i < ir.coefficients.size(); ++i)
        {
            ir.coefficients[i] *= std::exp(-lowpassStrength * static_cast<float>(i));
        }
    }
}

} // namespace Audio
} // namespace Engine