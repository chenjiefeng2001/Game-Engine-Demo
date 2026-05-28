#include "Engine/UIManager.h"
#include <iostream>

namespace Engine {

    IUIManager* UIManager::s_Instance = nullptr;
    std::unique_ptr<IUIManager> UIManager::s_InstanceOwner = nullptr;

    bool UIManager::Init(std::unique_ptr<IUIManager> instance,
                         void* nativeWindow, void* apiContext) {
        if (s_Instance) {
            std::cerr << "[UIManager] Already initialized" << std::endl;
            return true;
        }
        if (!instance) {
            std::cerr << "[UIManager] Null instance" << std::endl;
            return false;
        }

        if (!instance->Init(nativeWindow, apiContext)) {
            std::cerr << "[UIManager] Init failed" << std::endl;
            return false;
        }

        s_InstanceOwner = std::move(instance);
        s_Instance = s_InstanceOwner.get();
        return true;
    }

    void UIManager::Shutdown() {
        if (s_Instance) {
            s_Instance->Shutdown();
            s_Instance = nullptr;
            s_InstanceOwner.reset();
        }
    }

} // namespace Engine
