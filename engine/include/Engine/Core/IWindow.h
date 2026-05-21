#pragma once
#include <string>
#include <memory>
#include <stdint.h>

namespace Engine {
    class IRenderContext; // 前向声明

    class IWindow {
    public:
        virtual ~IWindow() = default;

        virtual void OnUpdate() = 0;
        virtual bool ShouldClose() const = 0;
        virtual IRenderContext* GetContext() = 0;

        // ⚠️ 必须加上这一行：静态工厂方法
        static std::unique_ptr<IWindow> Create(uint32_t width = 800, uint32_t height = 600, const std::string& title = "Engine");
    };
}