#include "Engine/Platform/GlfwLifetime.h"

#include <GLFW/glfw3.h>
#include <mutex>
#include <thread>

namespace Engine {
namespace {

std::mutex s_GlfwLifetimeMutex;
int s_GlfwLifetimeUsers = 0;
int s_GlfwLegacyUsers = 0;
int s_GlfwPersistentUsers = 0;
std::thread::id s_GlfwOwnerThread;
bool s_GlfwOwnerSet = false;
std::thread::id s_DeclaredMainThread;
bool s_DeclaredMainThreadSet = false;
int s_GlfwInitCount = 0;
int s_GlfwTerminateCount = 0;

}

bool RegisterGlfwOwnerThread() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    const std::thread::id current = std::this_thread::get_id();
    if (s_DeclaredMainThreadSet && s_DeclaredMainThread != current) return false;
    if (s_GlfwOwnerSet && s_GlfwOwnerThread != current) return false;
    if (!s_DeclaredMainThreadSet) {
        s_DeclaredMainThread = current;
        s_DeclaredMainThreadSet = true;
    }
    return true;
}

bool AcquireGlfwLifetime(bool persistent) {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    const std::thread::id current = std::this_thread::get_id();
    if (persistent && !s_DeclaredMainThreadSet) return false;
    if (s_DeclaredMainThreadSet && s_DeclaredMainThread != current) return false;
    if (s_GlfwOwnerSet && s_GlfwOwnerThread != current) return false;
    if (persistent && (s_GlfwLegacyUsers > 0 || s_GlfwPersistentUsers > 0)) return false;
    if (!persistent && s_GlfwPersistentUsers > 0) return false;
    if (s_GlfwLifetimeUsers == 0) {
        if (!glfwInit()) return false;
        if (!s_GlfwOwnerSet) {
            s_GlfwOwnerThread = current;
            s_GlfwOwnerSet = true;
        }
        ++s_GlfwInitCount;
    }
    ++s_GlfwLifetimeUsers;
    if (persistent) ++s_GlfwPersistentUsers;
    else ++s_GlfwLegacyUsers;
    return true;
}

void ReleaseGlfwLifetime(bool persistent) {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    if (!s_GlfwOwnerSet ||
        s_GlfwOwnerThread != std::this_thread::get_id() ||
        s_GlfwLifetimeUsers == 0) return;
    if (persistent) {
        if (s_GlfwPersistentUsers == 0) return;
        --s_GlfwPersistentUsers;
    } else {
        if (s_GlfwLegacyUsers == 0) return;
        --s_GlfwLegacyUsers;
    }
    --s_GlfwLifetimeUsers;
    if (s_GlfwLifetimeUsers == 0) {
        glfwTerminate();
        ++s_GlfwTerminateCount;
        s_GlfwOwnerSet = false;
    }
}

int GlfwLifetimeUsers() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    return s_GlfwLifetimeUsers;
}

int GlfwLegacyUsers() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    return s_GlfwLegacyUsers;
}

int GlfwPersistentUsers() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    return s_GlfwPersistentUsers;
}

int GlfwInitCount() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    return s_GlfwInitCount;
}

int GlfwTerminateCount() {
    std::lock_guard<std::mutex> lock(s_GlfwLifetimeMutex);
    return s_GlfwTerminateCount;
}

}
