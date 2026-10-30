// T-7-A deterministic runtime tests (A-T01..A-T15).
//
// These tests exercise the Stack 2 runtime contract without an audio device.
// All backend calls go through FakeOpenALBackend, which records calls and can
// inject failures. Rejected operations must produce zero backend calls.

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Engine/OpenAL/OpenALAudioEngine.h"
#include "Engine/OpenAL/OpenALBackend.h"
#include "Engine/Core/Audio/IAudioBuffer.h"
#include "Engine/Core/Audio/IAudioSource.h"

using namespace Engine;

namespace {

class FakeOpenALBackend final : public IOpenALBackend {
public:
    std::atomic<int> openDeviceCalls{0};
    std::atomic<int> closeDeviceCalls{0};
    std::atomic<int> createContextCalls{0};
    std::atomic<int> destroyContextCalls{0};
    std::atomic<int> makeCurrentCalls{0};
    std::atomic<int> clearCurrentCalls{0};
    std::atomic<int> genSourceCalls{0};
    std::atomic<int> deleteSourceCalls{0};
    std::atomic<int> sourcePlayCalls{0};
    std::atomic<int> sourceStopCalls{0};
    std::atomic<int> sourcePauseCalls{0};
    std::atomic<int> setSourceCalls{0};
    std::atomic<int> genBufferCalls{0};
    std::atomic<int> deleteBufferCalls{0};
    std::atomic<int> bufferDataCalls{0};
    mutable std::atomic<int> listenerCalls{0};
    std::atomic<int> distanceModelCalls{0};

    std::atomic<bool> failOpenDevice{false};
    std::atomic<bool> failCreateContext{false};
    std::atomic<bool> failMakeCurrent{false};
    std::atomic<bool> failGenSource{false};
    std::atomic<bool> failGenBuffer{false};
    std::atomic<bool> failBufferData{false};
    std::atomic<bool> failSourcePlay{false};
    std::atomic<bool> failDeleteSource{false};
    std::atomic<bool> failDeleteBuffer{false};

    std::atomic<bool> connected{true};
    std::atomic<bool> disconnectExtension{true};
    std::atomic<uint32> nextSource{1};
    std::atomic<uint32> nextBuffer{1};

    // EFX capability / fault injection / call counters.
    std::atomic<bool> efxExtension{true};
    std::atomic<bool> efxEntryPoints{true};
    std::atomic<int32> maxAuxiliarySends{4};
    mutable std::atomic<int> efxEntryCheckCalls{0};
    std::atomic<int> genEffectCalls{0};
    std::atomic<int> deleteEffectCalls{0};
    std::atomic<int> setEffectCalls{0};
    mutable std::atomic<int> getEffectCalls{0};
    std::atomic<int> genSlotCalls{0};
    std::atomic<int> deleteSlotCalls{0};
    std::atomic<int> setSlotCalls{0};
    std::atomic<int> genFilterCalls{0};
    std::atomic<int> deleteFilterCalls{0};
    std::atomic<int> setFilterCalls{0};
    mutable std::atomic<int> getFilterCalls{0};
    std::atomic<int> sourceAuxSendCalls{0};
    std::atomic<int> sourceDirectFilterCalls{0};
    std::atomic<bool> failGenEffect{false};
    std::atomic<bool> failDeleteEffect{false};
    std::atomic<bool> failSetEffect{false};
    std::atomic<bool> failGetEffect{false};
    std::atomic<bool> failGenSlot{false};
    std::atomic<bool> failDeleteSlot{false};
    std::atomic<bool> failSetSlot{false};
    std::atomic<bool> failGenFilter{false};
    std::atomic<bool> failDeleteFilter{false};
    std::atomic<bool> failSetFilter{false};
    std::atomic<bool> failGetFilter{false};
    std::atomic<bool> failSourceAuxSend{false};
    std::atomic<bool> failSourceDirectFilter{false};

    // ── Device / context ──
    bool OpenDevice() override {
        openDeviceCalls.fetch_add(1);
        Record("open_device");
        return !failOpenDevice.load();
    }
    void CloseDevice() override {
        closeDeviceCalls.fetch_add(1);
        Record("close_device");
    }
    bool CreateContext() override {
        createContextCalls.fetch_add(1);
        Record("create_context");
        return !failCreateContext.load();
    }
    void DestroyContext() override {
        destroyContextCalls.fetch_add(1);
        Record("destroy_context");
    }
    bool MakeContextCurrent() override {
        makeCurrentCalls.fetch_add(1);
        Record("make_current");
        return !failMakeCurrent.load();
    }
    void ClearContextCurrent() override {
        clearCurrentCalls.fetch_add(1);
        Record("clear_current");
    }
    void* GetDeviceHandle() const override { return nullptr; }
    void* GetContextHandle() const override { return nullptr; }
    bool IsExtensionPresent(const char* name) const override {
        if (!name) return false;
        if (std::strcmp(name, "ALC_EXT_disconnect") == 0) {
            return disconnectExtension.load();
        }
        if (std::strcmp(name, "ALC_EXT_EFX") == 0) {
            return efxExtension.load();
        }
        return false;
    }
    bool IsDeviceConnected() const override { return connected.load(); }

    // ── Listener / global ──
    bool SetDistanceModel(AttenuationMode) override {
        distanceModelCalls.fetch_add(1);
        return true;
    }
    bool SetListenerPosition(const Vec3&) override {
        listenerCalls.fetch_add(1);
        return true;
    }
    bool GetListenerPosition(Vec3& position) const override {
        listenerCalls.fetch_add(1);
        position = Vec3(0.0f, 0.0f, 0.0f);
        return true;
    }
    bool SetListenerOrientation(const Vec3&, const Vec3&) override {
        listenerCalls.fetch_add(1);
        return true;
    }
    bool SetListenerGain(float32) override {
        listenerCalls.fetch_add(1);
        return true;
    }
    bool GetListenerGain(float32& gain) const override {
        listenerCalls.fetch_add(1);
        gain = 1.0f;
        return true;
    }

    // ── Source ──
    uint32 GenSource() override {
        genSourceCalls.fetch_add(1);
        Record("gen_source");
        if (failGenSource.load()) return 0;
        return nextSource.fetch_add(1);
    }
    bool DeleteSource(uint32) override {
        deleteSourceCalls.fetch_add(1);
        Record("delete_source");
        return !failDeleteSource.load();
    }
    bool SourcePlay(uint32) override {
        sourcePlayCalls.fetch_add(1);
        Record("source_play");
        return !failSourcePlay.load();
    }
    bool SourceStop(uint32) override {
        sourceStopCalls.fetch_add(1);
        Record("source_stop");
        return true;
    }
    bool SourcePause(uint32) override {
        sourcePauseCalls.fetch_add(1);
        Record("source_pause");
        return true;
    }
    AudioBackendSourceState GetSourceState(uint32) const override {
        getSourceCalls.fetch_add(1);
        return AudioBackendSourceState::Stopped;
    }
    bool SetSourceInt(uint32, AudioBackendSourceParam, int32) override {
        setSourceCalls.fetch_add(1);
        return true;
    }
    bool SetSourceFloat(uint32, AudioBackendSourceParam, float32) override {
        setSourceCalls.fetch_add(1);
        return true;
    }
    bool SetSourceVec3(uint32, AudioBackendSourceParam, const Vec3&) override {
        setSourceCalls.fetch_add(1);
        return true;
    }
    bool GetSourceFloat(uint32, AudioBackendSourceParam, float32& value) const override {
        getSourceCalls.fetch_add(1);
        value = 0.0f;
        return true;
    }
    bool GetSourceVec3(uint32, AudioBackendSourceParam, Vec3& value) const override {
        getSourceCalls.fetch_add(1);
        value = Vec3(0.0f, 0.0f, 0.0f);
        return true;
    }

    // ── Buffer ──
    uint32 GenBuffer() override {
        genBufferCalls.fetch_add(1);
        Record("gen_buffer");
        if (failGenBuffer.load()) return 0;
        return nextBuffer.fetch_add(1);
    }
    bool DeleteBuffer(uint32) override {
        deleteBufferCalls.fetch_add(1);
        Record("delete_buffer");
        return !failDeleteBuffer.load();
    }
    bool BufferData(uint32, AudioFormat, const void*, int32, int32) override {
        bufferDataCalls.fetch_add(1);
        Record("buffer_data");
        return !failBufferData.load();
    }

    // ── EFX ──
    bool AreEfxEntryPointsAvailable() const override {
        efxEntryCheckCalls.fetch_add(1);
        return efxEntryPoints.load();
    }
    int32 GetMaxAuxiliarySends() const override { return maxAuxiliarySends.load(); }

    uint32 GenEffect() override {
        genEffectCalls.fetch_add(1);
        Record("gen_effect");
        if (failGenEffect.load()) return 0;
        const uint32 id = nextEffect.fetch_add(1);
        std::lock_guard<std::mutex> lock(efxMutex);
        effects[id];
        return id;
    }
    bool DeleteEffect(uint32 effect) override {
        deleteEffectCalls.fetch_add(1);
        Record("delete_effect");
        if (failDeleteEffect.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        effects.erase(effect);
        return true;
    }
    bool SetEffectInt(uint32 effect, AudioBackendEffectParam param,
                      int32 value) override {
        setEffectCalls.fetch_add(1);
        if (failSetEffect.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        effectInts[effect][param] = value;
        return true;
    }
    bool SetEffectFloat(uint32 effect, AudioBackendEffectParam param,
                        float32 value) override {
        setEffectCalls.fetch_add(1);
        if (failSetEffect.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        effectFloats[effect][param] = value;
        return true;
    }
    bool GetEffectFloat(uint32 effect, AudioBackendEffectParam param,
                        float32& value) const override {
        getEffectCalls.fetch_add(1);
        if (failGetEffect.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        auto object = effectFloats.find(effect);
        if (object == effectFloats.end()) return false;
        auto it = object->second.find(param);
        if (it == object->second.end()) return false;
        value = it->second;
        return true;
    }

    uint32 GenAuxiliarySlot() override {
        genSlotCalls.fetch_add(1);
        Record("gen_slot");
        if (failGenSlot.load()) return 0;
        const uint32 id = nextSlot.fetch_add(1);
        std::lock_guard<std::mutex> lock(efxMutex);
        slotEffects[id] = 0;
        return id;
    }
    bool DeleteAuxiliarySlot(uint32 slot) override {
        deleteSlotCalls.fetch_add(1);
        Record("delete_slot");
        if (failDeleteSlot.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        slotEffects.erase(slot);
        return true;
    }
    bool SetAuxiliarySlotInt(uint32 slot, AudioBackendSlotParam param,
                             int32 value) override {
        setSlotCalls.fetch_add(1);
        Record(value == 0 ? "detach_effect" : "attach_effect");
        if (failSetSlot.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        if (param == AudioBackendSlotParam::Effect) slotEffects[slot] = value;
        return true;
    }

    uint32 GenFilter() override {
        genFilterCalls.fetch_add(1);
        Record("gen_filter");
        if (failGenFilter.load()) return 0;
        const uint32 id = nextFilter.fetch_add(1);
        std::lock_guard<std::mutex> lock(efxMutex);
        filters[id];
        return id;
    }
    bool DeleteFilter(uint32 filter) override {
        deleteFilterCalls.fetch_add(1);
        Record("delete_filter");
        if (failDeleteFilter.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        filters.erase(filter);
        return true;
    }
    bool SetFilterInt(uint32 filter, AudioBackendFilterParam param,
                      int32 value) override {
        setFilterCalls.fetch_add(1);
        if (failSetFilter.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        filterInts[filter][param] = value;
        return true;
    }
    bool SetFilterFloat(uint32 filter, AudioBackendFilterParam param,
                        float32 value) override {
        setFilterCalls.fetch_add(1);
        if (failSetFilter.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        filterFloats[filter][param] = value;
        return true;
    }
    bool GetFilterFloat(uint32 filter, AudioBackendFilterParam param,
                        float32& value) const override {
        getFilterCalls.fetch_add(1);
        if (failGetFilter.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        auto object = filterFloats.find(filter);
        if (object == filterFloats.end()) return false;
        auto it = object->second.find(param);
        if (it == object->second.end()) return false;
        value = it->second;
        return true;
    }

    bool SetSourceAuxSend(uint32 source, int32 send,
                          uint32 slot, uint32 filter) override {
        sourceAuxSendCalls.fetch_add(1);
        Record(slot == 0 ? "disconnect_aux" : "source_aux_send");
        if (failSourceAuxSend.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        if (slot == 0) {
            sourceAux.erase(source);
        } else {
            sourceAux[source] = std::make_tuple(send, slot, filter);
        }
        return true;
    }
    bool SetSourceDirectFilter(uint32 source, uint32 filter) override {
        sourceDirectFilterCalls.fetch_add(1);
        Record(filter == 0 ? "unbind_direct" : "bind_direct");
        if (failSourceDirectFilter.load()) return false;
        std::lock_guard<std::mutex> lock(efxMutex);
        if (filter == 0) {
            sourceDirect.erase(source);
        } else {
            sourceDirect[source] = filter;
        }
        return true;
    }

    int32 SlotEffect(uint32 slot) const {
        std::lock_guard<std::mutex> lock(efxMutex);
        auto it = slotEffects.find(slot);
        return it == slotEffects.end() ? 0 : it->second;
    }
    bool HasSourceAux(uint32 source) const {
        std::lock_guard<std::mutex> lock(efxMutex);
        return sourceAux.find(source) != sourceAux.end();
    }
    std::tuple<int32, uint32, uint32> SourceAux(uint32 source) const {
        std::lock_guard<std::mutex> lock(efxMutex);
        auto it = sourceAux.find(source);
        return it == sourceAux.end() ? std::make_tuple(-1, 0u, 0u) : it->second;
    }
    bool HasDirectFilter(uint32 source) const {
        std::lock_guard<std::mutex> lock(efxMutex);
        return sourceDirect.find(source) != sourceDirect.end();
    }
    float32 FilterFloat(uint32 filter, AudioBackendFilterParam param) const {
        std::lock_guard<std::mutex> lock(efxMutex);
        auto object = filterFloats.find(filter);
        if (object == filterFloats.end()) return 0.0f;
        auto it = object->second.find(param);
        return it == object->second.end() ? 0.0f : it->second;
    }

    std::vector<std::string> Order() const {
        std::lock_guard<std::mutex> lock(orderMutex);
        return order;
    }

private:
    void Record(const char* event) {
        std::lock_guard<std::mutex> lock(orderMutex);
        order.emplace_back(event);
    }

    mutable std::atomic<int> getSourceCalls{0};
    std::atomic<uint32> nextEffect{1};
    std::atomic<uint32> nextSlot{1};
    std::atomic<uint32> nextFilter{1};
    mutable std::mutex efxMutex;
    std::unordered_map<uint32, std::map<AudioBackendEffectParam, int32>> effectInts;
    std::unordered_map<uint32, std::map<AudioBackendEffectParam, float32>> effectFloats;
    std::unordered_map<uint32, bool> effects;
    std::unordered_map<uint32, int32> slotEffects;
    std::unordered_map<uint32, std::map<AudioBackendFilterParam, int32>> filterInts;
    std::unordered_map<uint32, std::map<AudioBackendFilterParam, float32>> filterFloats;
    std::unordered_map<uint32, bool> filters;
    std::unordered_map<uint32, std::tuple<int32, uint32, uint32>> sourceAux;
    std::unordered_map<uint32, uint32> sourceDirect;
    mutable std::mutex orderMutex;
    std::vector<std::string> order;
};

AudioClipInfo MonoInfo() {
    AudioClipInfo info;
    info.sampleRate = 44100;
    info.format = AudioFormat::Mono16;
    info.dataSize = 4;
    return info;
}

std::shared_ptr<IAudioBuffer> MakeBuffer(OpenALAudioEngine& engine) {
    const int16 pcm[2] = { 0, 0 };
    return engine.CreateBuffer(pcm, sizeof(pcm), MonoInfo());
}

std::shared_ptr<IAudioSource> MakeSource(OpenALAudioEngine& engine) {
    return engine.CreateSource();
}

} // namespace

TEST(OpenALRuntimeTest, A_T01_InitBindsOwnerAndReachesReady) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);

    ASSERT_TRUE(engine.Init());
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::Ok);
    EXPECT_EQ(backend->openDeviceCalls.load(), 1);
    EXPECT_EQ(backend->createContextCalls.load(), 1);
    EXPECT_EQ(backend->makeCurrentCalls.load(), 1);

    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);
    EXPECT_NE(source->GetNativeHandle(), 0u);
    EXPECT_NE(buffer->GetNativeHandle(), 0u);
}

TEST(OpenALRuntimeTest, A_T02_RepeatInitDoesNotDuplicateDeviceOrContext) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);

    ASSERT_TRUE(engine.Init());
    ASSERT_TRUE(engine.Init());

    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
    EXPECT_EQ(backend->openDeviceCalls.load(), 1);
    EXPECT_EQ(backend->createContextCalls.load(), 1);
    EXPECT_EQ(backend->makeCurrentCalls.load(), 1);
}

TEST(OpenALRuntimeTest, A_T03_WrongThreadCallIsRejectedBeforeBackend) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    ASSERT_NE(source, nullptr);

    const int setBefore = backend->setSourceCalls.load();
    AudioStatus status = AudioStatus::Ok;
    std::thread worker([&] {
        status = source->SetVolume(0.5f);
    });
    worker.join();

    EXPECT_EQ(status, AudioStatus::WrongThread);
    EXPECT_EQ(backend->setSourceCalls.load(), setBefore);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);

    std::shared_ptr<IAudioSource> wrongThreadSource;
    std::thread creator([&] {
        wrongThreadSource = engine.CreateSource();
    });
    creator.join();
    EXPECT_EQ(wrongThreadSource, nullptr);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::WrongThread);
    EXPECT_EQ(backend->genSourceCalls.load(), 1);

    const int destroyBefore = backend->destroyContextCalls.load();
    AudioStatus shutdownStatus = AudioStatus::Ok;
    std::thread shutdownThread([&] {
        shutdownStatus = engine.Shutdown();
    });
    shutdownThread.join();
    EXPECT_EQ(shutdownStatus, AudioStatus::WrongThread);
    EXPECT_EQ(backend->destroyContextCalls.load(), destroyBefore);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
}

TEST(OpenALRuntimeTest, A_T04_WrongThreadDestructionDefersWithoutAl) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    const int sourceDeletes = backend->deleteSourceCalls.load();
    const int bufferDeletes = backend->deleteBufferCalls.load();

    std::thread worker([&] {
        source.reset();
        buffer.reset();
    });
    worker.join();

    EXPECT_EQ(backend->deleteSourceCalls.load(), sourceDeletes);
    EXPECT_EQ(backend->deleteBufferCalls.load(), bufferDeletes);

    ASSERT_EQ(engine.Update(), AudioStatus::Ok);
    EXPECT_EQ(backend->deleteSourceCalls.load(), sourceDeletes + 1);
    EXPECT_EQ(backend->deleteBufferCalls.load(), bufferDeletes + 1);
}

TEST(OpenALRuntimeTest, A_T05_ShutdownReleasesInOrder) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Shutdown);

    const auto order = backend->Order();
    auto indexOf = [&](const char* name) -> int {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    const int stop = indexOf("source_stop");
    const int deleteSource = indexOf("delete_source");
    const int deleteBuffer = indexOf("delete_buffer");
    const int destroyContext = indexOf("destroy_context");
    const int closeDevice = indexOf("close_device");
    ASSERT_GE(stop, 0);
    ASSERT_GE(deleteSource, 0);
    ASSERT_GE(deleteBuffer, 0);
    ASSERT_GE(destroyContext, 0);
    ASSERT_GE(closeDevice, 0);
    EXPECT_LT(stop, deleteSource);
    EXPECT_LT(deleteSource, deleteBuffer);
    EXPECT_LT(deleteBuffer, destroyContext);
    EXPECT_LT(destroyContext, closeDevice);
    EXPECT_LT(indexOf("clear_current"), destroyContext);
}

TEST(OpenALRuntimeTest, A_T06_ExternalSourceAfterShutdownDoesNotCallAl) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    const int playBefore = backend->sourcePlayCalls.load();
    const int stopBefore = backend->sourceStopCalls.load();

    EXPECT_EQ(source->Play(buffer), AudioStatus::NotInitialized);
    EXPECT_EQ(backend->sourcePlayCalls.load(), playBefore);
    source.reset();
    buffer.reset();
    EXPECT_EQ(backend->sourceStopCalls.load(), stopBefore);
}

TEST(OpenALRuntimeTest, A_T07_BufferCreationFailureReturnsNullAndCleansUp) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    backend->failBufferData.store(true);

    auto buffer = MakeBuffer(engine);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(backend->genBufferCalls.load(), 1);
    EXPECT_EQ(backend->deleteBufferCalls.load(), 1);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::BackendError);
}

TEST(OpenALRuntimeTest, A_T08_SourceCreationFailureReturnsNull) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    backend->failGenSource.store(true);

    auto source = MakeSource(engine);
    EXPECT_EQ(source, nullptr);
    EXPECT_EQ(backend->genSourceCalls.load(), 1);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::BackendError);
}

TEST(OpenALRuntimeTest, A_T09_DeviceDisconnectEntersFailed) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    backend->connected.store(false);
    EXPECT_EQ(engine.Update(), AudioStatus::DeviceLost);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Failed);

    const int playBefore = backend->sourcePlayCalls.load();
    EXPECT_EQ(source->Play(buffer), AudioStatus::DeviceLost);
    EXPECT_EQ(backend->sourcePlayCalls.load(), playBefore);
}

TEST(OpenALRuntimeTest, A_T10_FailedStateRejectsPlaybackAndCreation) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    backend->connected.store(false);
    ASSERT_EQ(engine.Update(), AudioStatus::DeviceLost);

    const int playBefore = backend->sourcePlayCalls.load();
    const int genBefore = backend->genSourceCalls.load();
    EXPECT_EQ(source->Play(buffer), AudioStatus::DeviceLost);
    EXPECT_EQ(engine.CreateSource(), nullptr);
    EXPECT_EQ(backend->sourcePlayCalls.load(), playBefore);
    EXPECT_EQ(backend->genSourceCalls.load(), genBefore);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::DeviceLost);
}

TEST(OpenALRuntimeTest, A_T11_RepeatShutdownIsIdempotent) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    const int destroyBefore = backend->destroyContextCalls.load();
    const int closeBefore = backend->closeDeviceCalls.load();
    const int deleteSourceBefore = backend->deleteSourceCalls.load();
    const int deleteBufferBefore = backend->deleteBufferCalls.load();

    EXPECT_EQ(engine.Shutdown(), AudioStatus::Ok);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Shutdown);
    EXPECT_EQ(backend->destroyContextCalls.load(), destroyBefore);
    EXPECT_EQ(backend->closeDeviceCalls.load(), closeBefore);
    EXPECT_EQ(backend->deleteSourceCalls.load(), deleteSourceBefore);
    EXPECT_EQ(backend->deleteBufferCalls.load(), deleteBufferBefore);
}

TEST(OpenALRuntimeTest, A_T12_InitFailureRequiresShutdownBeforeRetry) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    backend->failOpenDevice.store(true);

    EXPECT_FALSE(engine.Init());
    EXPECT_EQ(engine.GetState(), AudioEngineState::Failed);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::BackendError);

    EXPECT_FALSE(engine.Init());
    EXPECT_EQ(engine.GetState(), AudioEngineState::Failed);
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::InvalidState);
    EXPECT_EQ(backend->openDeviceCalls.load(), 1);

    backend->failOpenDevice.store(false);
    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Shutdown);
    ASSERT_TRUE(engine.Init());
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
    EXPECT_EQ(backend->openDeviceCalls.load(), 2);
    EXPECT_EQ(backend->createContextCalls.load(), 1);
    EXPECT_EQ(backend->makeCurrentCalls.load(), 1);
}

TEST(OpenALRuntimeTest, A_T13_SourceRetainsBufferUntilStop) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    ASSERT_EQ(source->Play(buffer), AudioStatus::Ok);
    EXPECT_EQ(engine.DestroyBuffer(buffer.get()), AudioStatus::InvalidState);
    EXPECT_EQ(backend->deleteBufferCalls.load(), 0);
    buffer.reset();
    EXPECT_EQ(backend->deleteBufferCalls.load(), 0);

    ASSERT_EQ(source->Stop(), AudioStatus::Ok);
    EXPECT_EQ(backend->deleteBufferCalls.load(), 0);
    ASSERT_EQ(engine.Update(), AudioStatus::Ok);
    EXPECT_EQ(backend->deleteBufferCalls.load(), 1);
}

TEST(OpenALRuntimeTest, A_T14_ExternalWrappersAfterEngineObjectDoNotCallAl) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    std::shared_ptr<IAudioSource> source;
    std::shared_ptr<IAudioBuffer> buffer;
    {
        OpenALAudioEngine engine(backend);
        ASSERT_TRUE(engine.Init());
        source = MakeSource(engine);
        buffer = MakeBuffer(engine);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(buffer, nullptr);
    }

    const int deleteSourceBefore = backend->deleteSourceCalls.load();
    const int deleteBufferBefore = backend->deleteBufferCalls.load();
    source.reset();
    buffer.reset();
    EXPECT_EQ(backend->deleteSourceCalls.load(), deleteSourceBefore);
    EXPECT_EQ(backend->deleteBufferCalls.load(), deleteBufferBefore);
}

TEST(OpenALRuntimeTest, A_T15_OldWrappersAreInertAfterReInit) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto oldSource = MakeSource(engine);
    auto oldBuffer = MakeBuffer(engine);
    ASSERT_NE(oldSource, nullptr);
    ASSERT_NE(oldBuffer, nullptr);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    ASSERT_TRUE(engine.Init());

    const int playBefore = backend->sourcePlayCalls.load();
    EXPECT_EQ(oldSource->Play(oldBuffer), AudioStatus::NotInitialized);
    EXPECT_EQ(backend->sourcePlayCalls.load(), playBefore);
    EXPECT_EQ(oldBuffer->GetNativeHandle(), 0u);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
}

TEST(OpenALRuntimeTest, A_T16_CrossEngineResourcesAreRejected) {
    auto backendA = std::make_shared<FakeOpenALBackend>();
    auto backendB = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engineA(backendA);
    OpenALAudioEngine engineB(backendB);
    ASSERT_TRUE(engineA.Init());
    ASSERT_TRUE(engineB.Init());

    auto sourceA = MakeSource(engineA);
    auto bufferA = MakeBuffer(engineA);
    auto sourceB = MakeSource(engineB);
    auto bufferB = MakeBuffer(engineB);
    ASSERT_NE(sourceA, nullptr);
    ASSERT_NE(bufferA, nullptr);
    ASSERT_NE(sourceB, nullptr);
    ASSERT_NE(bufferB, nullptr);

    // Foreign source/buffer must not be destroyed through another engine.
    const int deleteSourceBefore = backendB->deleteSourceCalls.load();
    const int deleteBufferBefore = backendB->deleteBufferCalls.load();
    EXPECT_EQ(engineB.DestroySource(sourceA.get()), AudioStatus::NotFound);
    EXPECT_EQ(engineB.DestroyBuffer(bufferA.get()), AudioStatus::NotFound);
    EXPECT_EQ(backendB->deleteSourceCalls.load(), deleteSourceBefore);
    EXPECT_EQ(backendB->deleteBufferCalls.load(), deleteBufferBefore);

    // A source must not bind a buffer belonging to another engine.
    const int playBefore = backendA->sourcePlayCalls.load();
    EXPECT_EQ(sourceA->Play(bufferB), AudioStatus::InvalidArgument);
    EXPECT_EQ(backendA->sourcePlayCalls.load(), playBefore);
}

namespace {

int FindFrom(const std::vector<std::string>& order, const char* name, int start) {
    for (int i = start; i < static_cast<int>(order.size()); ++i) {
        if (order[static_cast<size_t>(i)] == name) return i;
    }
    return -1;
}

} // namespace

TEST(OpenALEfxTest, E_T01_CapabilitySupported) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    backend->maxAuxiliarySends.store(4);
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto caps = engine.GetEfxCapabilities();
    EXPECT_TRUE(caps.supported);
    EXPECT_TRUE(caps.routingSupported);
    EXPECT_EQ(caps.maxAuxiliarySends, 4);
    EXPECT_EQ(caps.status, AudioStatus::Ok);
    EXPECT_EQ(backend->efxEntryCheckCalls.load(), 1);

    const auto reverb = engine.CreateReverb();
    ASSERT_TRUE(reverb.IsValid());
    EXPECT_EQ(reverb.type, AudioEfxType::Reverb);
    EXPECT_EQ(backend->genEffectCalls.load(), 1);
}

TEST(OpenALEfxTest, E_T02_UnsupportedKeepsBaseAudioWorking) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    backend->efxExtension.store(false);
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto caps = engine.GetEfxCapabilities();
    EXPECT_FALSE(caps.supported);
    EXPECT_FALSE(caps.routingSupported);
    EXPECT_EQ(caps.status, AudioStatus::Unsupported);

    const auto reverb = engine.CreateReverb();
    EXPECT_FALSE(reverb.IsValid());
    EXPECT_EQ(backend->genEffectCalls.load(), 0);
    EXPECT_EQ(backend->efxEntryCheckCalls.load(), 0);

    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(source->Play(buffer), AudioStatus::Ok);
    ASSERT_EQ(source->Stop(), AudioStatus::Ok);
}

TEST(OpenALEfxTest, E_T02B_EfxEntryPointFailureIsBackendError) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    backend->efxEntryPoints.store(false);
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto caps = engine.GetEfxCapabilities();
    EXPECT_FALSE(caps.supported);
    EXPECT_EQ(caps.status, AudioStatus::BackendError);
    EXPECT_TRUE(engine.CreateReverb() == AudioEfxHandle{});
    EXPECT_EQ(backend->genEffectCalls.load(), 0);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
}

TEST(OpenALEfxTest, E_T02C_NoAuxiliarySendMakesRoutingUnsupported) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    backend->maxAuxiliarySends.store(0);
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto caps = engine.GetEfxCapabilities();
    EXPECT_TRUE(caps.supported);
    EXPECT_FALSE(caps.routingSupported);
    EXPECT_EQ(caps.status, AudioStatus::Ok);

    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    auto source = MakeSource(engine);
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(engine.ConnectAuxSend(source.get(), slot, 0.5f),
              AudioStatus::Unsupported);
}

TEST(OpenALEfxTest, E_T02D_CapabilityIsRequeriedAfterReInit) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    backend->efxExtension.store(false);
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    EXPECT_FALSE(engine.GetEfxCapabilities().supported);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    backend->efxExtension.store(true);
    ASSERT_TRUE(engine.Init());
    const auto caps = engine.GetEfxCapabilities();
    EXPECT_TRUE(caps.supported);
    EXPECT_EQ(caps.status, AudioStatus::Ok);
}

TEST(OpenALEfxTest, E_T03_ReverbLifecycleAndReadback) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto reverb = engine.CreateReverb();
    ASSERT_TRUE(reverb.IsValid());

    AudioReverbParameters params;
    params.density = 0.5f;
    params.diffusion = 0.4f;
    params.gain = 0.2f;
    params.gainHF = 0.8f;
    params.decayTime = 2.0f;
    params.decayHFRatio = 0.5f;
    ASSERT_EQ(engine.SetReverbParameters(reverb, params), AudioStatus::Ok);

    AudioReverbParameters readback;
    ASSERT_EQ(engine.GetReverbParameters(reverb, readback), AudioStatus::Ok);
    EXPECT_NEAR(readback.density, params.density, 1e-4f);
    EXPECT_NEAR(readback.diffusion, params.diffusion, 1e-4f);
    EXPECT_NEAR(readback.gain, params.gain, 1e-4f);
    EXPECT_NEAR(readback.gainHF, params.gainHF, 1e-4f);
    EXPECT_NEAR(readback.decayTime, params.decayTime, 1e-4f);
    EXPECT_NEAR(readback.decayHFRatio, params.decayHFRatio, 1e-4f);

    auto invalid = params;
    invalid.density = -0.1f;
    EXPECT_EQ(engine.SetReverbParameters(reverb, invalid),
              AudioStatus::InvalidArgument);
    invalid = params;
    invalid.gain = std::numeric_limits<float32>::quiet_NaN();
    EXPECT_EQ(engine.SetReverbParameters(reverb, invalid),
              AudioStatus::InvalidArgument);

    EXPECT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::Ok);
    EXPECT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::NotFound);
    EXPECT_EQ(backend->deleteEffectCalls.load(), 1);
}

TEST(OpenALEfxTest, E_T04_AuxiliarySlotLifecycle) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());

    ASSERT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::Ok);
    EXPECT_EQ(backend->setSlotCalls.load(), 1);
    EXPECT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::InvalidState);
    EXPECT_EQ(engine.DetachEffect(slot), AudioStatus::Ok);
    EXPECT_EQ(backend->setSlotCalls.load(), 2);
    EXPECT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::Ok);
    EXPECT_EQ(engine.DestroyEfxObject(slot), AudioStatus::Ok);
    EXPECT_EQ(backend->deleteEffectCalls.load(), 1);
    EXPECT_EQ(backend->deleteSlotCalls.load(), 1);

    const auto order = backend->Order();
    const int attach = FindFrom(order, "attach_effect", 0);
    const int detach = FindFrom(order, "detach_effect", 0);
    const int deleteEffect = FindFrom(order, "delete_effect", 0);
    const int deleteSlot = FindFrom(order, "delete_slot", 0);
    ASSERT_GE(attach, 0);
    ASSERT_GE(detach, attach);
    ASSERT_GE(deleteEffect, detach);
    ASSERT_GE(deleteSlot, deleteEffect);
}

TEST(OpenALEfxTest, E_T05_SourceAuxRoutingAndSendLevel) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    ASSERT_NE(source, nullptr);

    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());
    ASSERT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::Ok);

    const uint32 sourceID = source->GetNativeHandle();
    ASSERT_EQ(engine.ConnectAuxSend(source.get(), slot, 1.7f),
              AudioStatus::Ok);
    EXPECT_TRUE(backend->HasSourceAux(sourceID));
    const auto route = backend->SourceAux(sourceID);
    EXPECT_EQ(std::get<0>(route), 0);
    EXPECT_NE(std::get<1>(route), 0u);
    EXPECT_NE(std::get<2>(route), 0u);
    EXPECT_NEAR(backend->FilterFloat(std::get<2>(route),
                                     AudioBackendFilterParam::Gain),
                1.0f, 1e-4f);
    EXPECT_NEAR(backend->FilterFloat(std::get<2>(route),
                                     AudioBackendFilterParam::GainHF),
                1.0f, 1e-4f);
    EXPECT_EQ(backend->genFilterCalls.load(), 1);

    EXPECT_EQ(engine.ConnectAuxSend(source.get(), slot,
                                    std::numeric_limits<float32>::infinity()),
              AudioStatus::InvalidArgument);
    EXPECT_EQ(backend->genFilterCalls.load(), 1);

    AudioEfxHandle invalidSlot;
    invalidSlot.engineId = slot.engineId;
    invalidSlot.generation = slot.generation;
    invalidSlot.serial = slot.serial + 999;
    invalidSlot.type = AudioEfxType::AuxiliarySlot;
    EXPECT_EQ(engine.ConnectAuxSend(source.get(), invalidSlot, 0.5f),
              AudioStatus::NotFound);

    ASSERT_EQ(engine.DisconnectAuxSend(source.get()), AudioStatus::Ok);
    EXPECT_FALSE(backend->HasSourceAux(sourceID));
    EXPECT_EQ(backend->deleteFilterCalls.load(), 1);
    EXPECT_EQ(engine.DisconnectAuxSend(source.get()), AudioStatus::Ok);
}

TEST(OpenALEfxTest, E_T06_LowPassFilterLifecycleAndBinding) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    ASSERT_NE(source, nullptr);

    const auto filter = engine.CreateLowPassFilter();
    ASSERT_TRUE(filter.IsValid());

    AudioLowPassParameters params;
    params.gain = 0.5f;
    params.gainHF = 0.25f;
    ASSERT_EQ(engine.SetLowPassParameters(filter, params), AudioStatus::Ok);
    AudioLowPassParameters readback;
    ASSERT_EQ(engine.GetLowPassParameters(filter, readback), AudioStatus::Ok);
    EXPECT_NEAR(readback.gain, params.gain, 1e-4f);
    EXPECT_NEAR(readback.gainHF, params.gainHF, 1e-4f);

    auto invalid = params;
    invalid.gain = 1.2f;
    EXPECT_EQ(engine.SetLowPassParameters(filter, invalid),
              AudioStatus::InvalidArgument);

    const uint32 sourceID = source->GetNativeHandle();
    ASSERT_EQ(engine.BindDirectFilter(source.get(), filter), AudioStatus::Ok);
    EXPECT_TRUE(backend->HasDirectFilter(sourceID));
    EXPECT_EQ(engine.DestroyEfxObject(filter), AudioStatus::InvalidState);
    ASSERT_EQ(engine.UnbindDirectFilter(source.get()), AudioStatus::Ok);
    EXPECT_FALSE(backend->HasDirectFilter(sourceID));
    ASSERT_EQ(engine.DestroyEfxObject(filter), AudioStatus::Ok);
    EXPECT_EQ(backend->deleteFilterCalls.load(), 1);
}

TEST(OpenALEfxTest, E_T07_WrongThreadEfxCallsAreRejected) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());

    AudioEfxHandle handle;
    std::thread worker([&] { handle = engine.CreateReverb(); });
    worker.join();

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::WrongThread);
    EXPECT_EQ(backend->genEffectCalls.load(), 0);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
}

TEST(OpenALEfxTest, E_T08_ShutdownReleasesEfxDependenciesInOrder) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    const auto filter = engine.CreateLowPassFilter();
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());
    ASSERT_TRUE(filter.IsValid());
    ASSERT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::Ok);
    ASSERT_EQ(engine.ConnectAuxSend(source.get(), slot, 0.5f),
              AudioStatus::Ok);
    ASSERT_EQ(engine.BindDirectFilter(source.get(), filter), AudioStatus::Ok);

    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);

    const auto order = backend->Order();
    const int disconnect = FindFrom(order, "disconnect_aux", 0);
    const int firstDeleteFilter = FindFrom(order, "delete_filter", 0);
    const int unbind = FindFrom(order, "unbind_direct", 0);
    const int secondDeleteFilter = FindFrom(order, "delete_filter",
                                            firstDeleteFilter + 1);
    const int detach = FindFrom(order, "detach_effect", 0);
    const int deleteEffect = FindFrom(order, "delete_effect", 0);
    const int deleteSlot = FindFrom(order, "delete_slot", 0);
    const int deleteSource = FindFrom(order, "delete_source", 0);
    const int deleteBuffer = FindFrom(order, "delete_buffer", 0);
    const int destroyContext = FindFrom(order, "destroy_context", 0);

    ASSERT_GE(disconnect, 0);
    ASSERT_GE(firstDeleteFilter, disconnect);
    ASSERT_GE(unbind, firstDeleteFilter);
    ASSERT_GE(secondDeleteFilter, unbind);
    ASSERT_GE(detach, secondDeleteFilter);
    ASSERT_GE(deleteEffect, detach);
    ASSERT_GE(deleteSlot, deleteEffect);
    ASSERT_GE(deleteSource, deleteSlot);
    ASSERT_GE(deleteBuffer, deleteSource);
    ASSERT_GE(destroyContext, deleteBuffer);
    EXPECT_EQ(backend->deleteFilterCalls.load(), 2);
}

TEST(OpenALEfxTest, E_T09_DeviceLostRejectsEfxWithoutBackendCalls) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());

    backend->connected.store(false);
    ASSERT_EQ(engine.Update(), AudioStatus::DeviceLost);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Failed);

    const int genBefore = backend->genEffectCalls.load();
    EXPECT_EQ(engine.CreateReverb(), AudioEfxHandle{});
    EXPECT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::DeviceLost);
    EXPECT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::DeviceLost);
    EXPECT_EQ(backend->genEffectCalls.load(), genBefore);
}

TEST(OpenALEfxTest, E_T10_EfxBackendErrorKeepsEngineReady) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    backend->failSetEffect.store(true);

    EXPECT_FALSE(engine.CreateReverb().IsValid());
    EXPECT_EQ(engine.GetLastStatus(), AudioStatus::BackendError);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Ready);
    EXPECT_EQ(engine.GetEfxCapabilities().status, AudioStatus::Ok);
    EXPECT_EQ(backend->deleteEffectCalls.load(), 1);
}

TEST(OpenALEfxTest, E_T11_SourceDestructionClosesEfxRoutes) {
    auto backend = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engine(backend);
    ASSERT_TRUE(engine.Init());
    auto source = MakeSource(engine);
    auto buffer = MakeBuffer(engine);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(buffer, nullptr);

    const auto reverb = engine.CreateReverb();
    const auto slot = engine.CreateAuxiliarySlot();
    const auto filter = engine.CreateLowPassFilter();
    ASSERT_TRUE(reverb.IsValid());
    ASSERT_TRUE(slot.IsValid());
    ASSERT_TRUE(filter.IsValid());
    ASSERT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::Ok);

    const uint32 sourceID = source->GetNativeHandle();
    ASSERT_EQ(source->Play(buffer), AudioStatus::Ok);
    ASSERT_EQ(engine.ConnectAuxSend(source.get(), slot, 0.5f),
              AudioStatus::Ok);
    ASSERT_EQ(engine.BindDirectFilter(source.get(), filter), AudioStatus::Ok);

    source.reset();
    ASSERT_EQ(engine.Update(), AudioStatus::Ok);
    EXPECT_FALSE(backend->HasSourceAux(sourceID));
    EXPECT_FALSE(backend->HasDirectFilter(sourceID));
    EXPECT_GE(backend->deleteFilterCalls.load(), 1);
    EXPECT_NE(buffer->GetNativeHandle(), 0u);
    EXPECT_EQ(engine.DestroyBuffer(buffer.get()), AudioStatus::Ok);
}

TEST(OpenALEfxTest, E_T14_CrossEngineAndOldGenerationEfxHandles) {
    auto backendA = std::make_shared<FakeOpenALBackend>();
    auto backendB = std::make_shared<FakeOpenALBackend>();
    OpenALAudioEngine engineA(backendA);
    OpenALAudioEngine engineB(backendB);
    ASSERT_TRUE(engineA.Init());
    ASSERT_TRUE(engineB.Init());

    const auto reverbA = engineA.CreateReverb();
    const auto slotA = engineA.CreateAuxiliarySlot();
    auto sourceA = MakeSource(engineA);
    ASSERT_TRUE(reverbA.IsValid());
    ASSERT_TRUE(slotA.IsValid());
    ASSERT_NE(sourceA, nullptr);

    const int deleteBefore = backendB->deleteEffectCalls.load();
    EXPECT_EQ(engineB.DestroyEfxObject(reverbA), AudioStatus::NotFound);
    EXPECT_EQ(backendB->deleteEffectCalls.load(), deleteBefore);
    EXPECT_EQ(engineB.ConnectAuxSend(sourceA.get(), slotA, 0.5f),
              AudioStatus::NotFound);

    ASSERT_EQ(engineA.Shutdown(), AudioStatus::Ok);
    ASSERT_TRUE(engineA.Init());
    const auto params = AudioReverbParameters{};
    EXPECT_EQ(engineA.SetReverbParameters(reverbA, params),
              AudioStatus::NotFound);
    EXPECT_TRUE(engineA.CreateReverb().IsValid());
}

// Auxiliary real-device smoke. The deterministic A-T matrix is the gate; this
// test only validates the real backend adapter when a default device exists.
TEST(OpenALRuntimeDeviceSmoke, InitCreatePlayStopShutdown) {
    OpenALAudioEngine engine;
    if (!engine.Init()) {
        GTEST_SKIP() << "No default OpenAL device available; status="
                     << static_cast<int>(engine.GetLastStatus());
    }

    ASSERT_EQ(engine.GetState(), AudioEngineState::Ready);

    int16 pcm[256] = {};
    AudioClipInfo info;
    info.sampleRate = 44100;
    info.format = AudioFormat::Mono16;
    info.dataSize = static_cast<int32>(sizeof(pcm));

    auto buffer = engine.CreateBuffer(pcm, sizeof(pcm), info);
    ASSERT_NE(buffer, nullptr);
    auto source = engine.CreateSource();
    ASSERT_NE(source, nullptr);

    ASSERT_EQ(source->SetVolume(0.0f), AudioStatus::Ok);
    ASSERT_EQ(source->Play(buffer), AudioStatus::Ok);
    ASSERT_EQ(source->Stop(), AudioStatus::Ok);
    ASSERT_EQ(engine.Update(), AudioStatus::Ok);
    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
    EXPECT_EQ(engine.GetState(), AudioEngineState::Shutdown);
}

TEST(OpenALDeviceEfxSmoke, InitConfigurePlayShutdown) {
    OpenALAudioEngine engine;
    if (!engine.Init()) {
        GTEST_SKIP() << "No default OpenAL device available; status="
                     << static_cast<int>(engine.GetLastStatus());
    }

    const auto caps = engine.GetEfxCapabilities();
    if (!caps.supported) {
        GTEST_SKIP() << "EFX unsupported by this provider; status="
                     << static_cast<int>(caps.status);
    }
    if (!caps.routingSupported) {
        GTEST_SKIP() << "No auxiliary send 0 available";
    }

    const auto reverb = engine.CreateReverb();
    ASSERT_TRUE(reverb.IsValid());
    const auto slot = engine.CreateAuxiliarySlot();
    ASSERT_TRUE(slot.IsValid());
    ASSERT_EQ(engine.AttachEffect(slot, reverb), AudioStatus::Ok);

    auto source = engine.CreateSource();
    ASSERT_NE(source, nullptr);
    int16 pcm[256] = {};
    AudioClipInfo info;
    info.sampleRate = 44100;
    info.format = AudioFormat::Mono16;
    info.dataSize = static_cast<int32>(sizeof(pcm));
    auto buffer = engine.CreateBuffer(pcm, sizeof(pcm), info);
    ASSERT_NE(buffer, nullptr);

    ASSERT_EQ(engine.ConnectAuxSend(source.get(), slot, 0.5f),
              AudioStatus::Ok);
    const auto filter = engine.CreateLowPassFilter();
    ASSERT_TRUE(filter.IsValid());
    ASSERT_EQ(engine.BindDirectFilter(source.get(), filter), AudioStatus::Ok);
    ASSERT_EQ(source->SetVolume(0.0f), AudioStatus::Ok);
    ASSERT_EQ(source->Play(buffer), AudioStatus::Ok);
    ASSERT_EQ(source->Stop(), AudioStatus::Ok);

    ASSERT_EQ(engine.DisconnectAuxSend(source.get()), AudioStatus::Ok);
    ASSERT_EQ(engine.UnbindDirectFilter(source.get()), AudioStatus::Ok);
    ASSERT_EQ(engine.DestroyEfxObject(filter), AudioStatus::Ok);
    ASSERT_EQ(engine.DetachEffect(slot), AudioStatus::Ok);
    ASSERT_EQ(engine.DestroyEfxObject(reverb), AudioStatus::Ok);
    ASSERT_EQ(engine.DestroyEfxObject(slot), AudioStatus::Ok);
    ASSERT_EQ(engine.Shutdown(), AudioStatus::Ok);
}
