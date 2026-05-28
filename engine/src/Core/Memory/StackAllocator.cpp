#include "Engine/Core/Memory/StackAllocator.h"
#include <cstdlib>
#include <iostream>

namespace Engine {

StackAllocator::StackAllocator(size_t capacity)
    : m_Capacity(capacity)
    , m_Offset(0)
{
    m_Memory = static_cast<std::byte*>(std::malloc(capacity));
    if (!m_Memory) {
        std::cerr << "[StackAllocator] Failed to allocate "
                  << capacity << " bytes" << std::endl;
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
        std::cerr << "[StackAllocator] Out of memory: requested " << size
                  << " aligned to " << alignment
                  << ", used " << m_Offset << "/" << m_Capacity << std::endl;
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
