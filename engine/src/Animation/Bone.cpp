/**
 * @file Bone.cpp
 * @brief 骨骼实现 — 矩阵计算
 */

#include "Engine/Animation/Bone.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

    void Bone::ComputeInverseBind() {
        // 将 Engine::Mat4 转为 glm::mat4
        glm::mat4 glmBind = glm::make_mat4(bindMatrix.Data());

        // 计算逆矩阵
        glm::mat4 glmInverse = glm::inverse(glmBind);

        // 写回 Engine::Mat4
        std::memcpy(inverseBindMatrix.Data(), glm::value_ptr(glmInverse), sizeof(float32) * 16);
    }

} // namespace Engine
