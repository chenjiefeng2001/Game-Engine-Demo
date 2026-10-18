#pragma once

namespace Engine {

bool RegisterGlfwOwnerThread();
bool AcquireGlfwLifetime(bool persistent = false);
void ReleaseGlfwLifetime(bool persistent = false);
int GlfwLifetimeUsers();
int GlfwLegacyUsers();
int GlfwPersistentUsers();
int GlfwInitCount();
int GlfwTerminateCount();

}
