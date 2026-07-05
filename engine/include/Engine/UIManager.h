#pragma once

/**
 * @file UIManager.h
 * @brief UI 管理器静态代理 — 将 IUIManager 接口暴露为便捷静态方法
 *
 * 内部持有 IUIManager 实例指针，所有静态方法转发到该实例。
 * IUIManager 实例由 IGraphicsFactory::CreateUIManager() 创建，
 * 通过 UIManager::Init() 注入。
 *
 * 调用方只需 UIManager::Get()->Begin()，无需关心具体实现。
 */

#include "Engine/Types.h"
#include "Engine/Core/IUIManager.h"
#include <memory>

namespace Engine {

    class UIManager {
    public:
        static bool Init(std::unique_ptr<IUIManager> instance,
                         void* nativeWindow, void* apiContext);
        static void Shutdown();
        static IUIManager* Get() { return s_Instance; }

        static void Begin()          { if (s_Instance) s_Instance->Begin(); }
        static void End()            { if (s_Instance) s_Instance->End(); }
        static void SetVisible(bool v)     { if (s_Instance) s_Instance->SetVisible(v); }
        static bool IsVisible()            { return s_Instance && s_Instance->IsVisible(); }
        static void ToggleVisibility()     { if (s_Instance) s_Instance->ToggleVisibility(); }
        static bool WantCaptureMouse()     { return s_Instance && s_Instance->WantCaptureMouse(); }
        static bool WantCaptureKeyboard()  { return s_Instance && s_Instance->WantCaptureKeyboard(); }
        static bool IsInitialized()        { return s_Instance && s_Instance->IsInitialized(); }
        static void SetScale(float scale)  { if (s_Instance) s_Instance->SetScale(scale); }
        static float GetScale()            { return s_Instance ? s_Instance->GetScale() : 1.0f; }

    private:
        static IUIManager* s_Instance;
        static std::unique_ptr<IUIManager> s_InstanceOwner;
    };

} // namespace Engine
