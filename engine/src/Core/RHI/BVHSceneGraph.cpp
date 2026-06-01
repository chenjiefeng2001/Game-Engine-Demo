#include "Engine/Core/RHI/BVHSceneGraph.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace Engine {

    // ═══════════════════════════════════════════════
    // AABB / Frustum 实现在 SceneGraphShared.cpp
    // ═══════════════════════════════════════════════

    // ============================================================
    // BVH 构建
    // ============================================================

    void BVHSceneGraph::Build(std::vector<GameObject*>& objects) {
        Clear();
        if (objects.empty()) return;

        m_Objects.clear();
        m_ObjectBounds.clear();

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh()) continue;

            auto mesh = meshComp->GetMesh();
            const auto& verts = mesh->GetVertices();
            if (verts.empty()) continue;

            AABB localBounds;
            for (auto& v : verts) localBounds.Expand(v.position);

            const Mat4& worldMat = obj->GetTransform().GetWorldMatrix();
            AABB worldBounds = localBounds.Transformed(worldMat);
            m_Objects.push_back(obj);
            m_ObjectBounds.push_back(worldBounds);
        }

        if (m_Objects.empty()) return;

        m_Nodes.reserve(m_Objects.size() * 2);
        BuildRecursive(0, (uint32)m_Objects.size(), 0);
    }

    void BVHSceneGraph::Clear() {
        m_Nodes.clear();
        m_Objects.clear();
        m_ObjectBounds.clear();
        m_FrustumValid = false;
        m_LastVisibleCount = 0;
    }

    // ============================================================
    // ISceneGraph 接口实现
    // ============================================================

    void BVHSceneGraph::SetFrustum(const PerspectiveCamera* camera) {
        if (!camera) { m_FrustumValid = false; return; }
        const float* vpData = const_cast<PerspectiveCamera*>(camera)->GetViewProjectionMatrixPtr();
        Mat4 vpMat;
        std::memcpy(vpMat.Data(), vpData, sizeof(float) * 16);
        m_Frustum.Extract(vpMat);
        m_FrustumValid = true;
    }

    void BVHSceneGraph::GetVisibleObjects(std::vector<GameObject*>& outObjects) const {
        outObjects.clear();
        m_LastVisibleCount = 0;
        if (!IsValid()) return;

        if (!m_FrustumValid) {
            outObjects = m_Objects;
            m_LastVisibleCount = (uint32)m_Objects.size();
            return;
        }
        Traverse(0, outObjects);
        m_LastVisibleCount = (uint32)outObjects.size();
    }

    SceneGraphStats BVHSceneGraph::GetStats() const {
        SceneGraphStats s;
        s.totalObjects = (uint32)m_Objects.size();
        s.visibleCount = m_LastVisibleCount;
        s.nodeCount    = (uint32)m_Nodes.size();
        s.cullRatio    = m_Objects.empty() ? 1.0f :
                         (float)m_LastVisibleCount / (float)m_Objects.size();
        return s;
    }

    // ============================================================
    // 递归构建
    // ============================================================

    uint32 BVHSceneGraph::BuildRecursive(uint32 start, uint32 end, uint32 depth) {
        uint32 nodeIdx = (uint32)m_Nodes.size();
        m_Nodes.emplace_back();
        auto& node = m_Nodes.back();

        AABB bounds;
        for (uint32 i = start; i < end; ++i)
            bounds.Expand(m_ObjectBounds[i]);
        node.bounds = bounds;

        uint32 count = end - start;
        if (count <= m_MaxLeafSize || depth >= kMaxDepth) {
            node.startIndex = start;
            node.endIndex   = end;
            return nodeIdx;
        }

        Vec3 size(bounds.max.x - bounds.min.x,
                  bounds.max.y - bounds.min.y,
                  bounds.max.z - bounds.min.z);
        int axis = (size.y > size.x) ? 1 : 0;
        if (size.z > (axis == 0 ? size.x : size.y)) axis = 2;

        SortByAxis(start, end, axis);
        uint32 mid = start + count / 2;

        node.leftChild  = BuildRecursive(start, mid, depth + 1);
        node.rightChild = BuildRecursive(mid, end, depth + 1);
        return nodeIdx;
    }

    void BVHSceneGraph::SortByAxis(uint32 start, uint32 end, int axis) {
        std::vector<uint32> indices(end - start);
        for (uint32 i = 0; i < (uint32)indices.size(); ++i)
            indices[i] = start + i;

        std::sort(indices.begin(), indices.end(),
            [&](uint32 a, uint32 b) {
                Vec3 ca = m_ObjectBounds[a].Center();
                Vec3 cb = m_ObjectBounds[b].Center();
                return (&ca.x)[axis] < (&cb.x)[axis];
            });

        std::vector<GameObject*> newObjs(end - start);
        std::vector<AABB> newBounds(end - start);
        for (uint32 i = 0; i < (uint32)indices.size(); ++i) {
            newObjs[i]   = m_Objects[indices[i]];
            newBounds[i] = m_ObjectBounds[indices[i]];
        }
        std::copy(newObjs.begin(),   newObjs.end(),   m_Objects.begin()   + start);
        std::copy(newBounds.begin(), newBounds.end(), m_ObjectBounds.begin() + start);
    }

    // ============================================================
    // 递归遍历
    // ============================================================

    void BVHSceneGraph::Traverse(uint32 nodeIndex, std::vector<GameObject*>& outObjects) const {
        if (nodeIndex >= m_Nodes.size()) return;
        const auto& node = m_Nodes[nodeIndex];

        if (m_FrustumValid && !m_Frustum.TestAABB(node.bounds))
            return;

        if (node.isLeaf()) {
            for (uint32 i = node.startIndex; i < node.endIndex; ++i)
                if (i < (uint32)m_Objects.size())
                    outObjects.push_back(m_Objects[i]);
        } else {
            Traverse(node.leftChild, outObjects);
            Traverse(node.rightChild, outObjects);
        }
    }

} // namespace Engine
