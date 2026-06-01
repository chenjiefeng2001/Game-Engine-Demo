#pragma once
/**
 * @file DecalSystem.h
 * @brief 贴花系统 — 在表面投射纹理
 *
 * 贴花使用球体/盒体投影，在延迟渲染或前向渲染中以全屏四边形
 * 或几何体方式渲染。简化实现：使用半透明公告板带深度测试。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/IGraphicsFactory.h"
#include <memory>
#include <vector>

namespace Engine {

    class Texture;
    class Shader;
    class IPrimitiveBatch;
    class PerspectiveCamera;

    struct Decal {
        Vec3  position;
        Vec3  normal = {0, 1, 0};
        Vec3  scale  = {1, 1, 1};
        float angle  = 0;
        float life   = -1;       // -1 = 永久
        float maxLife = -1;
        bool  active = true;
        std::shared_ptr<Texture> texture;

        // 旋转四元数辅助
        Vec3 right;
        Vec3 up;
    };

    class DecalSystem {
    public:
        DecalSystem() = default;
        ~DecalSystem() = default;

        /** 添加永久贴花 */
        void AddDecal(const Vec3& pos, const Vec3& normal,
                      std::shared_ptr<Texture> tex,
                      const Vec3& scale = Vec3(1,1,1));

        /** 添加有时效的贴花 */
        void AddDecalTimed(const Vec3& pos, const Vec3& normal,
                           std::shared_ptr<Texture> tex,
                           float lifeTime,
                           const Vec3& scale = Vec3(1,1,1));

        void Update(float dt);
        void Render(IPrimitiveBatch& batch, Shader* shader,
                    PerspectiveCamera* camera);

        void Clear() { m_Decals.clear(); }

        int GetCount() const { return (int)m_Decals.size(); }

    private:
        void ComputeBasis(Decal& d);
        std::vector<Decal> m_Decals;
    };

} // namespace Engine
