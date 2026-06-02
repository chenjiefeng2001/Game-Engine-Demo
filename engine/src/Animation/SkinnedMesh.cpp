/**
 * @file SkinnedMesh.cpp
 * @brief 蒙皮网格实现
 */

#include "Engine/Animation/SkinnedMesh.h"

namespace Engine {

    void SkinnedMesh::RemapBoneIndices(const std::vector<int32>& mapping) {
        m_BoneMapping = mapping;
        for (auto& vert : m_Vertices) {
            for (int i = 0; i < 4; ++i) {
                uint32 idx = vert.boneIndices[i];
                if (idx < static_cast<uint32>(mapping.size())) {
                    vert.boneIndices[i] = static_cast<uint32>(mapping[idx]);
                }
            }
        }
    }

} // namespace Engine
