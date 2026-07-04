/**
 * @file LODSystem.cpp
 * @brief LOD/HLOD 选择系统实现
 */

#include "Engine/Rendering/LODSystem.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <cmath>

namespace {
    Engine::Logger s_Log("LODSystem");
}

namespace Engine {
namespace Rendering {

    void LODSystem::RegisterAsset(std::shared_ptr<LODAsset> asset) {
        if (!asset) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Assets[asset->assetID.Value()] = std::move(asset);
    }

    void LODSystem::UnregisterAsset(StringID assetID) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Assets.erase(assetID.Value());
        m_CustomSelectors.erase(assetID.Value());
    }

    uint32_t LODSystem::SelectByScreenSize(const LODAsset& asset, float screenSize) const noexcept {
        if (asset.lodLevels.empty()) return 0;

        float adjustedSize = screenSize * asset.lodBias;
        uint32_t bestLOD = 0;

        for (uint32_t i = 0; i < asset.lodLevels.size(); ++i) {
            const auto& level = asset.lodLevels[i];
            if (adjustedSize <= level.pixelError || i == asset.lodLevels.size() - 1) {
                bestLOD = i;
                break;
            }
        }
        return bestLOD;
    }

    LODSelectionResult LODSystem::SelectLOD(StringID assetID, float screenSize, float distance) const {
        LODSelectionResult result;
        result.distance = distance;
        result.screenSize = screenSize;
        result.selectedLOD = 0;
        result.transition = 0.0f;
        result.isHLODSelected = false;

        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Assets.find(assetID.Value());
        if (it == m_Assets.end()) return result;

        const auto& asset = *it->second;

        // 自定义选择器优先
        auto selIt = m_CustomSelectors.find(assetID.Value());
        if (selIt != m_CustomSelectors.end() && selIt->second) {
            result.selectedLOD = selIt->second(asset, screenSize, distance);
        } else {
            result.selectedLOD = SelectByScreenSize(asset, screenSize);
        }

        // 检查 HLOD
        if (result.selectedLOD < asset.lodLevels.size()) {
            const auto& level = asset.lodLevels[result.selectedLOD];
            result.isHLODSelected = level.isHLOD;

            // 计算过渡系数
            if (result.selectedLOD + 1 < asset.lodLevels.size()) {
                float currentDist = asset.lodLevels[result.selectedLOD].pixelError;
                float nextDist    = asset.lodLevels[result.selectedLOD + 1].pixelError;
                if (nextDist > currentDist) {
                    float range = nextDist - currentDist;
                    if (range > 0.0001f) {
                        result.transition = std::clamp((screenSize - currentDist) / range, 0.0f, 1.0f);
                    }
                }
            }
        }

        return result;
    }

    void LODSystem::SetCustomSelector(StringID assetID, CustomSelector selector) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CustomSelectors[assetID.Value()] = std::move(selector);
    }

    float LODSystem::ScreenSizeToDistance(float screenSize, float objectRadius,
                                          float fovRadians, float screenHeight) {
        if (screenSize < 0.0001f || objectRadius < 0.0001f) return 1000.0f;
        // d = r / (s * tan(fov/2) / h)
        return objectRadius / (screenSize * std::tan(fovRadians * 0.5f) / screenHeight);
    }

} // namespace Rendering
} // namespace Engine