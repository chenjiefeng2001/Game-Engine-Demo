#include "Engine/Core/RHI/FlatSceneGraph.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include <chrono>

namespace Engine {

    void FlatSceneGraph::SetFrustum(const PerspectiveCamera* camera) {
        if (!camera) { m_FrustumValid = false; return; }
        const float* vpData = const_cast<PerspectiveCamera*>(camera)->GetViewProjectionMatrixPtr();
        Mat4 vpMat;
        std::memcpy(vpMat.Data(), vpData, sizeof(float) * 16);
        m_Frustum.Extract(vpMat);
        m_FrustumValid = true;
    }

    void FlatSceneGraph::SetFrustum(const Frustum& frustum) {
        m_Frustum = frustum;
        m_FrustumValid = true;
    }

    void FlatSceneGraph::GetVisibleObjects(std::vector<GameObject*>& outObjects) const {
        outObjects.clear();
        if (!m_FrustumValid) {
            outObjects = m_Objects;
            m_LastVisible = (uint32)m_Objects.size();
            return;
        }
        // 线性剔除：对每个物体做平截头体测试
        outObjects.reserve(m_Objects.size());
        for (auto* obj : m_Objects) {
            if (!obj || !obj->IsActive()) continue;
            // 简单测试：用物体位置代替完整 AABB
            const Vec3& pos = obj->GetTransform().GetPosition();
            bool inside = true;
            for (int i = 0; i < 6; ++i) {
                const Vec4& pl = m_Frustum.planes[i];
                float d = pos.x*pl.x + pos.y*pl.y + pos.z*pl.z + pl.w;
                if (d < -0.1f) { inside = false; break; }
            }
            if (inside) outObjects.push_back(obj);
        }
        m_LastVisible = (uint32)outObjects.size();
    }

    SceneGraphStats FlatSceneGraph::GetStats() const {
        SceneGraphStats s;
        s.totalObjects = (uint32)m_Objects.size();
        s.visibleCount = m_LastVisible;
        s.nodeCount = 0;
        s.cullRatio = m_Objects.empty() ? 1.0f :
                      (float)m_LastVisible / (float)m_Objects.size();
        return s;
    }

} // namespace Engine
