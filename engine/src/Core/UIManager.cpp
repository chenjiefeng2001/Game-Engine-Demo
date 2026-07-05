#include "Engine/UIManager.h"
#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("UIManager");
}

namespace Engine {

    IUIManager* UIManager::s_Instance = nullptr;
    std::unique_ptr<IUIManager> UIManager::s_InstanceOwner = nullptr;

    bool UIManager::Init(std::unique_ptr<IUIManager> instance,
                         void* nativeWindow, void* apiContext) {
        if (s_Instance) {
            s_Log.Error("Already initialized");
            return true;
        }
        if (!instance) {
            s_Log.Error("Null instance");
            return false;
        }

        if (!instance->Init(nativeWindow, apiContext)) {
            s_Log.Error("Init failed");
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
