#include "Engine/Animation/Locator.h"
#include "Engine/Animation/AnimationBlend.h"
#include <cmath>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 内部数学辅助
    // ============================================================

    // 四元数乘法: result = a * b
    static Quat QuatMul(const Quat& a, const Quat& b) {
        return Quat(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        );
    }

    // 四元数旋转向量: v' = q * v * q⁻¹
    static Vec3 QuatRotate(const Quat& q, const Vec3& v) {
        // q * (0, v) * q_conjugate
        Quat p(v.x, v.y, v.z, 0.0f);
        Quat qConj(-q.x, -q.y, -q.z, q.w);
        Quat result = QuatMul(QuatMul(q, p), qConj);
        return Vec3(result.x, result.y, result.z);
    }

    // 从 Mat4 提取缩放因子
    static Vec3 ExtractScale(const Mat4& m) {
        Vec3 scale;
        scale.x = std::sqrt(m.data[0] * m.data[0] + m.data[1] * m.data[1] + m.data[2] * m.data[2]);
        scale.y = std::sqrt(m.data[4] * m.data[4] + m.data[5] * m.data[5] + m.data[6] * m.data[6]);
        scale.z = std::sqrt(m.data[8] * m.data[8] + m.data[9] * m.data[9] + m.data[10] * m.data[10]);
        return scale;
    }

    // ============================================================
    // Locator::Compute
    // ============================================================
    void Locator::Compute(const Skeleton* skeleton) {
        if (worldSpace && boneIndex < 0) {
            // 世界空间固定点，无需计算
            return;
        }

        if (skeleton && boneIndex >= 0 &&
            boneIndex < static_cast<int32>(skeleton->GetBoneCount())) {

            const Bone& bone = skeleton->GetBone(boneIndex);
            const Mat4& m = bone.currentPoseMatrix;

            // 提取平移
            worldPosition.x = m.data[12];
            worldPosition.y = m.data[13];
            worldPosition.z = m.data[14];

            // 提取旋转
            worldRotation = AnimationBlend::FromMatrix(m);

            // 提取缩放
            worldScale = ExtractScale(m);

        } else if (worldSpace) {
            // 骨骼不存在或索引无效，保持已有值
            worldRotation = Quat::Identity();
            worldScale = Vec3(1, 1, 1);
        }

        // 应用局部偏移
        Vec3 rotatedOffset = QuatRotate(worldRotation, localPosition);
        worldPosition.x += rotatedOffset.x;
        worldPosition.y += rotatedOffset.y;
        worldPosition.z += rotatedOffset.z;

        worldRotation = QuatMul(worldRotation, localRotation);

        worldScale.x *= localScale.x;
        worldScale.y *= localScale.y;
        worldScale.z *= localScale.z;
    }

    // ============================================================
    // LocatorSystem
    // ============================================================

    int32 LocatorSystem::AddLocator(const Locator& locator) {
        auto it = std::find_if(m_Locators.begin(), m_Locators.end(),
            [&](const Locator& l) { return l.name == locator.name; });

        if (it != m_Locators.end()) {
            *it = locator; // 替换已有
            return static_cast<int32>(std::distance(m_Locators.begin(), it));
        }

        m_Locators.push_back(locator);
        return static_cast<int32>(m_Locators.size() - 1);
    }

    void LocatorSystem::RemoveLocator(const std::string& name) {
        auto it = std::remove_if(m_Locators.begin(), m_Locators.end(),
            [&](const Locator& l) { return l.name == name; });
        m_Locators.erase(it, m_Locators.end());
    }

    Locator* LocatorSystem::FindLocator(const std::string& name) {
        auto it = std::find_if(m_Locators.begin(), m_Locators.end(),
            [&](const Locator& l) { return l.name == name; });
        return it != m_Locators.end() ? &(*it) : nullptr;
    }

    const Locator* LocatorSystem::FindLocator(const std::string& name) const {
        auto it = std::find_if(m_Locators.begin(), m_Locators.end(),
            [&](const Locator& l) { return l.name == name; });
        return it != m_Locators.end() ? &(*it) : nullptr;
    }

    void LocatorSystem::Clear() {
        m_Locators.clear();
    }

    void LocatorSystem::Update(const Skeleton* skeleton) {
        for (auto& loc : m_Locators) {
            loc.Compute(skeleton);
        }
    }

    void LocatorSystem::UpdateForBone(const Skeleton* skeleton, int32 filterBoneIndex) {
        for (auto& loc : m_Locators) {
            if (loc.boneIndex == filterBoneIndex) {
                loc.Compute(skeleton);
            }
        }
    }

    void LocatorSystem::ImportLocators(const std::vector<Locator>& external) {
        for (const auto& ext : external) {
            AddLocator(ext);
        }
    }

} // namespace Engine
