#pragma once

#include "Engine/Core/RHI/ISceneGraph.h"

namespace Engine {

    /// 线性场景图 — 无空间剔除，直接遍历所有物体
    /// 适用于小场景（< 50 物体），零构建开销
    class FlatSceneGraph : public ISceneGraph {
    public:
        FlatSceneGraph() = default;
        virtual ~FlatSceneGraph() override = default;

        virtual SceneGraphType GetType() const override { return SceneGraphType::Flat; }
        virtual const char* GetName() const override { return "Flat Scene Graph"; }

        virtual void Build(std::vector<GameObject*>& objects) override { m_Objects = objects; }
        virtual void Clear() override { m_Objects.clear(); m_FrustumValid = false; }
        virtual bool IsValid() const override { return !m_Objects.empty(); }

        virtual void SetFrustum(const PerspectiveCamera* camera) override;
        virtual void SetFrustum(const Frustum& frustum) override;
        virtual const Frustum& GetFrustum() const override { return m_Frustum; }
        virtual bool HasFrustum() const override { return m_FrustumValid; }

        virtual void GetVisibleObjects(std::vector<GameObject*>& outObjects) const override;
        virtual uint32 GetObjectCount() const override { return (uint32)m_Objects.size(); }

        virtual SceneGraphStats GetStats() const override;

    private:
        std::vector<GameObject*> m_Objects;
        Frustum m_Frustum;
        bool m_FrustumValid = false;
        mutable uint32 m_LastVisible = 0;
    };

} // namespace Engine
