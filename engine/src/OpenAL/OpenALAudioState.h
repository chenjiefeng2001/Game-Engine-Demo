#pragma once

#include "Engine/OpenAL/OpenALBackend.h"
#include "Engine/Core/Audio/IAudioEfx.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

    class AudioEngineRuntime {
    public:
        explicit AudioEngineRuntime(std::shared_ptr<IOpenALBackend> b)
            : backend(std::move(b)),
              instanceId(AllocateInstanceId()) {}

        AudioEngineRuntime(const AudioEngineRuntime&) = delete;
        AudioEngineRuntime& operator=(const AudioEngineRuntime&) = delete;

        AudioStatus CheckOwnerReady() const {
            const auto current = phase.load(std::memory_order_acquire);
            if (current == AudioEngineState::Ready ||
                current == AudioEngineState::Failed) {
                if (ownerThread.load(std::memory_order_acquire) !=
                    std::this_thread::get_id()) {
                    return AudioStatus::WrongThread;
                }
            }

            switch (current) {
                case AudioEngineState::Uninitialized: return AudioStatus::NotInitialized;
                case AudioEngineState::Ready:         return AudioStatus::Ok;
                case AudioEngineState::Failed:        return AudioStatus::DeviceLost;
                case AudioEngineState::ShuttingDown:  return AudioStatus::ShuttingDown;
                case AudioEngineState::Shutdown:      return AudioStatus::NotInitialized;
            }
            return AudioStatus::InvalidState;
        }

        AudioStatus CheckOwnerReady(uint64_t expectedGeneration) const {
            if (expectedGeneration != generation.load(std::memory_order_acquire)) {
                return AudioStatus::NotInitialized;
            }
            return CheckOwnerReady();
        }

        AudioStatus ReportBackendFailure() {
            const bool hasDisconnectExtension =
                disconnectExtension.load(std::memory_order_acquire);
            if (!backend || !backend->IsDeviceConnected() ||
                !hasDisconnectExtension) {
                // Without ALC_EXT_disconnect there is no reliable health probe;
                // any core backend failure is treated as fail-closed.
                phase.store(AudioEngineState::Failed, std::memory_order_release);
                lastStatus.store(AudioStatus::DeviceLost, std::memory_order_release);
                return AudioStatus::DeviceLost;
            }
            lastStatus.store(AudioStatus::BackendError, std::memory_order_release);
            return AudioStatus::BackendError;
        }

        bool IsOwnerThread() const {
            return ownerThread.load(std::memory_order_acquire) == std::this_thread::get_id();
        }

        void SetLastStatus(AudioStatus status) const {
            lastStatus.store(status, std::memory_order_release);
        }

        AudioStatus GetLastStatus() const {
            return lastStatus.load(std::memory_order_acquire);
        }

        void RegisterSource(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            sources.insert(id);
        }

        bool UnregisterSource(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            return sources.erase(id) != 0;
        }

        void RegisterBuffer(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            buffers.insert(id);
        }

        bool UnregisterBuffer(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            return buffers.erase(id) != 0;
        }

        void AddBufferBinding(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            ++bufferBindings[id];
        }

        void RemoveBufferBinding(uint32_t id) {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = bufferBindings.find(id);
            if (it == bufferBindings.end()) return;
            if (--it->second <= 0) bufferBindings.erase(it);
        }

        bool IsBufferBound(uint32_t id) const {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = bufferBindings.find(id);
            return it != bufferBindings.end() && it->second > 0;
        }

        std::vector<uint32_t> SnapshotSources() const {
            std::lock_guard<std::mutex> lock(registryMutex);
            return { sources.begin(), sources.end() };
        }

        std::vector<uint32_t> SnapshotBuffers() const {
            std::lock_guard<std::mutex> lock(registryMutex);
            return { buffers.begin(), buffers.end() };
        }

        void ClearRegistries() {
            std::lock_guard<std::mutex> lock(registryMutex);
            sources.clear();
            buffers.clear();
            bufferBindings.clear();
        }

        void EnqueueSourceRelease(uint32_t id, uint64_t sourceGeneration) {
            EnqueueRelease(true, id, sourceGeneration);
        }

        void EnqueueBufferRelease(uint32_t id, uint64_t bufferGeneration) {
            EnqueueRelease(false, id, bufferGeneration);
        }

        void DrainDeferred() {
            std::vector<PendingRelease> pending;
            {
                std::lock_guard<std::mutex> lock(deferredMutex);
                pending.swap(deferred);
            }

            for (const auto& entry : pending) {
                if (entry.generation != generation.load(std::memory_order_acquire)) {
                    continue;
                }
                if (!backend) continue;
                if (entry.isSource) {
                    if (onSourceRelease) onSourceRelease(entry.id);
                    if (UnregisterSource(entry.id)) backend->DeleteSource(entry.id);
                } else {
                    if (!IsBufferBound(entry.id) && UnregisterBuffer(entry.id)) {
                        backend->DeleteBuffer(entry.id);
                    }
                }
            }
        }

        void ClearDeferred() {
            std::lock_guard<std::mutex> lock(deferredMutex);
            deferred.clear();
        }

        std::shared_ptr<IOpenALBackend> backend;
        const uint32 instanceId;
        std::function<void(uint32)> onSourceRelease;
        std::atomic<AudioEngineState> phase{AudioEngineState::Uninitialized};
        mutable std::atomic<AudioStatus> lastStatus{AudioStatus::NotInitialized};
        std::atomic<std::thread::id> ownerThread{};
        std::atomic<uint64_t> generation{0};
        std::atomic<bool> disconnectExtension{false};

    private:
        static uint32 AllocateInstanceId() {
            static std::atomic<uint32_t> next{1};
            return next.fetch_add(1, std::memory_order_relaxed);
        }

        struct PendingRelease {
            bool isSource = false;
            uint32_t id = 0;
            uint64_t generation = 0;
        };

        void EnqueueRelease(bool isSource, uint32_t id, uint64_t sourceGeneration) {
            if (phase.load(std::memory_order_acquire) != AudioEngineState::Ready) return;
            if (sourceGeneration != generation.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> lock(deferredMutex);
            deferred.push_back({ isSource, id, sourceGeneration });
        }

        mutable std::mutex registryMutex;
        std::unordered_set<uint32_t> sources;
        std::unordered_set<uint32_t> buffers;
        std::unordered_map<uint32_t, int> bufferBindings;

        std::mutex deferredMutex;
        std::vector<PendingRelease> deferred;
    };

} // namespace Engine
