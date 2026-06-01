#include "Engine/Core/RHI/ISceneGraph.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Engine {

    // ============================================================
    // AABB
    // ============================================================

    void AABB::Expand(const Vec3& point) {
        if (point.x < min.x) min.x = point.x;
        if (point.y < min.y) min.y = point.y;
        if (point.z < min.z) min.z = point.z;
        if (point.x > max.x) max.x = point.x;
        if (point.y > max.y) max.y = point.y;
        if (point.z > max.z) max.z = point.z;
    }

    void AABB::Expand(const AABB& other) {
        Expand(other.min); Expand(other.max);
    }

    AABB AABB::Transformed(const Mat4& worldMatrix) const {
        glm::mat4 mat = glm::make_mat4(worldMatrix.Data());
        AABB result;
        Vec3 corners[8] = {
            {min.x,min.y,min.z}, {max.x,min.y,min.z},
            {min.x,max.y,min.z}, {max.x,max.y,min.z},
            {min.x,min.y,max.z}, {max.x,min.y,max.z},
            {min.x,min.y,max.z}, {max.x,min.y,max.z},
        };
        // Fix the last two corners
        corners[6] = Vec3(min.x, max.y, max.z);
        corners[7] = Vec3(max.x, max.y, max.z);
        for (int i = 0; i < 8; ++i) {
            glm::vec4 wp = mat * glm::vec4(corners[i].x, corners[i].y, corners[i].z, 1.0f);
            result.Expand(Vec3(wp.x, wp.y, wp.z));
        }
        return result;
    }

    // ============================================================
    // Frustum
    // ============================================================

    void Frustum::Extract(const Mat4& viewProj) {
        const float* m = viewProj.Data();
        // 列主序矩阵, 索引 col*4+row

        // 左: 第 3 行 + 第 0 行
        planes[Left].x = m[3]  + m[0];
        planes[Left].y = m[7]  + m[4];
        planes[Left].z = m[11] + m[8];
        planes[Left].w = m[15] + m[12];

        // 右: 第 3 行 - 第 0 行
        planes[Right].x = m[3]  - m[0];
        planes[Right].y = m[7]  - m[4];
        planes[Right].z = m[11] - m[8];
        planes[Right].w = m[15] - m[12];

        // 底: 第 3 行 + 第 1 行
        planes[Bottom].x = m[3]  + m[1];
        planes[Bottom].y = m[7]  + m[5];
        planes[Bottom].z = m[11] + m[9];
        planes[Bottom].w = m[15] + m[13];

        // 顶: 第 3 行 - 第 1 行
        planes[Top].x = m[3]  - m[1];
        planes[Top].y = m[7]  - m[5];
        planes[Top].z = m[11] - m[9];
        planes[Top].w = m[15] - m[13];

        // 近: 第 3 行 + 第 2 行
        planes[Near].x = m[3]  + m[2];
        planes[Near].y = m[7]  + m[6];
        planes[Near].z = m[11] + m[10];
        planes[Near].w = m[15] + m[14];

        // 远: 第 3 行 - 第 2 行
        planes[Far].x = m[3]  - m[2];
        planes[Far].y = m[7]  - m[6];
        planes[Far].z = m[11] - m[10];
        planes[Far].w = m[15] - m[14];

        for (int i = 0; i < 6; ++i) {
            float len = std::sqrt(planes[i].x*planes[i].x +
                                   planes[i].y*planes[i].y +
                                   planes[i].z*planes[i].z);
            if (len > 0.0001f) {
                planes[i].x /= len; planes[i].y /= len;
                planes[i].z /= len; planes[i].w /= len;
            }
        }
    }

    bool Frustum::TestAABB(const AABB& aabb) const {
        for (int i = 0; i < 6; ++i) {
            const Vec4& pl = planes[i];
            Vec3 pv = aabb.min;
            if (pl.x >= 0) pv.x = aabb.max.x;
            if (pl.y >= 0) pv.y = aabb.max.y;
            if (pl.z >= 0) pv.z = aabb.max.z;
            float dot = pv.x*pl.x + pv.y*pl.y + pv.z*pl.z + pl.w;
            if (dot < 0) return false;
        }
        return true;
    }

} // namespace Engine
