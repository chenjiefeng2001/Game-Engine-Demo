#include "Engine/Core/Memory/StackAllocator.h"
#include "Engine/Core/Log.h"
#include <cstdlib>

namespace {
    Engine::Logger s_Log("StackAllocator");
}

namespace Engine {

StackAllocator::StackAllocator(size_t capacity)
    : m_Capacity(capacity)
    , m_Offset(0)
{
    m_Memory = static_cast<std::byte*>(std::malloc(capacity));
    if (!m_Memory) {
        s_Log.Error("Failed to allocate {} bytes", capacity);
        m_Capacity = 0;
    }
}

StackAllocator::~StackAllocator() {
    std::free(m_Memory);
    m_Memory = nullptr;
    m_Capacity = 0;
    m_Offset = 0;
}

void* StackAllocator::Allocate(size_t size, size_t alignment) {
    if (!m_Memory || size == 0) return nullptr;

    // 对齐当前偏移
    size_t aligned = (m_Offset + alignment - 1) & ~(alignment - 1);

    if (aligned + size > m_Capacity) {
        s_Log.Error("Out of memory: requested {} aligned to {}, used {}/{}", size, alignment, m_Offset, m_Capacity);
        return nullptr;
    }

    m_Offset = aligned + size;
    return m_Memory + aligned;
}

void StackAllocator::FreeTo(size_t marker) {
    if (marker <= m_Offset && marker <= m_Capacity) {
        m_Offset = marker;
    }
}

} // namespace Engine
