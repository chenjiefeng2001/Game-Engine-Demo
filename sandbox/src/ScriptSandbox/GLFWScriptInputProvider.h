#pragma once

/**
 * @file GLFWScriptInputProvider.h
 * @brief GLFW → IScriptInputProvider 单向适配器（Scripting v1）
 *
 * 边界规则：Lua 绑定层永远不接触 GLFW —— 换 SDL/Win32/手柄后端时
 * 只替换本适配器，Script API 契约不动。
 *
 * 键名映射（MVP 子集，按需扩充）：
 *   "W"~"Z", "0"~"9", "Space", "Escape", "Enter", "Tab",
 *   "LeftShift"/"RightShift"/"LeftCtrl"/"RightCtrl",
 *   "Up","Down","Left","Right", "F1"~"F12"
 */

#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Core/Input.h"
#include <string>

namespace Engine::Sandbox {

class GLFWScriptInputProvider final : public Scripting::GameplayAPI::IScriptInputProvider {
public:
    bool IsKeyDown(const char* keyName) override {
        const KeyCode code = MapKeyCode(keyName);
        if (code == KeyCode::COUNT) return false;   // 未识别键名安全降级
        return Input::IsKeyDown(code);
    }

    static KeyCode MapKeyCode(const std::string& name) {
        // 单字母
        if (name.size() == 1) {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            if (c >= 'A' && c <= 'Z') return static_cast<KeyCode>(c - 'A' + static_cast<int>(KeyCode::A));
            if (c >= '0' && c <= '9') return static_cast<KeyCode>(c - '0' + static_cast<int>(KeyCode::Num0));
        }
        struct Pair { const char* n; KeyCode k; };
        static const Pair kNamed[] = {
            { "Space",     KeyCode::Space },
            { "Escape",    KeyCode::Escape },
            { "Enter",     KeyCode::Enter },
            { "Tab",       KeyCode::Tab },
            { "Backspace", KeyCode::Backspace },
            { "LeftShift", KeyCode::LeftShift },  { "RightShift", KeyCode::RightShift },
            { "LeftCtrl",  KeyCode::LeftCtrl },   { "RightCtrl",  KeyCode::RightCtrl },
            { "LeftAlt",   KeyCode::LeftAlt },    { "RightAlt",   KeyCode::RightAlt },
            { "Up",        KeyCode::Up },  { "Down",  KeyCode::Down },
            { "Left",      KeyCode::Left },{ "Right", KeyCode::Right },
            { "F1", KeyCode::F1 }, { "F2", KeyCode::F2 }, { "F3", KeyCode::F3 },
            { "F4", KeyCode::F4 }, { "F5", KeyCode::F5 }, { "F6", KeyCode::F6 },
            { "F7", KeyCode::F7 }, { "F8", KeyCode::F8 }, { "F9", KeyCode::F9 },
            { "F10", KeyCode::F10 }, { "F11", KeyCode::F11 }, { "F12", KeyCode::F12 },
        };
        for (const auto& p : kNamed)
            if (name == p.n) return p.k;
        return KeyCode::COUNT;   // 未识别 → IsKeyDown(COUNT) 越界保护见下
    }
};

} // namespace Engine::Sandbox
