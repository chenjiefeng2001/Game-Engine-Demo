#include "Engine/Editor/Reflect.h"

namespace Engine {
namespace Reflect {

    // ── 全局注册表 ──
    static std::unordered_map<std::type_index, std::unique_ptr<MetaClass>>& GetRegistry() {
        static std::unordered_map<std::type_index, std::unique_ptr<MetaClass>> s_Registry;
        return s_Registry;
    }

    void RegisterClass(const std::type_index& typeIndex, std::unique_ptr<MetaClass> meta) {
        GetRegistry()[typeIndex] = std::move(meta);
    }

    const MetaClass* GetClass(const std::type_index& typeIndex) {
        auto& registry = GetRegistry();
        auto it = registry.find(typeIndex);
        if (it != registry.end()) {
            return it->second.get();
        }
        return nullptr;
    }

} // namespace Reflect
} // namespace Engine