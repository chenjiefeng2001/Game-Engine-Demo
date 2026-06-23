#include "Engine/Core/GameObject/Component.h"
#include <cstring>

namespace Engine {

    const char* Component::ParseTypeName(const char* mangledName) {
        // MSVC 的 typeid().name() 返回类似 "class Engine::SpriteComponent"
        // 去掉 "class " / "struct " 前缀，保留可读名称
        if (!mangledName) return "Unknown";

        const char* result = mangledName;

        // 跳过 "class " 前缀 (6个字符)
        if (std::strncmp(result, "class ", 6) == 0)
            result += 6;
        // 跳过 "struct " 前缀 (7个字符)
        else if (std::strncmp(result, "struct ", 7) == 0)
            result += 7;

        // 查找命名空间分隔符 "::"，如果存在则只保留最后的类名
        const char* lastColon = result;
        for (const char* p = result; *p; ++p) {
            if (*p == ':' && *(p + 1) == ':')
                lastColon = p + 2;
        }

        return lastColon;
    }

} // namespace Engine
