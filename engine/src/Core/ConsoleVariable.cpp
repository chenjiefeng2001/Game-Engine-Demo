#include "Engine/ConsoleVariable.h"

namespace Engine {

// ============================================================
// CVarRegistry 单例
// ============================================================

CVarRegistry& CVarRegistry::Instance() {
    static CVarRegistry instance;
    return instance;
}

// ============================================================
// 注册 / 查找 / 清空
// ============================================================

void CVarRegistry::Register(CVarBase* cvar) {
    if (cvar) {
        m_CVars[cvar->GetName()] = cvar;
    }
}

CVarBase* CVarRegistry::Find(const std::string& name) {
    auto it = m_CVars.find(name);
    if (it != m_CVars.end())
        return it->second;
    return nullptr;
}

void CVarRegistry::Clear() {
    m_CVars.clear();
}

} // namespace Engine
