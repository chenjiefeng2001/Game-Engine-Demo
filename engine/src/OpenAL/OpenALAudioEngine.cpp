#include "Engine/OpenAL/OpenALAudioEngine.h"

#include "OpenALAudioState.h"
#include "OpenALBackend.h"
#include "Engine/Core/Log.h"
#include "Engine/OpenAL/OpenALAudioBuffer.h"
#include "Engine/OpenAL/OpenALAudioSource.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
    Engine::Logger s_Log("OpenAL");
}

namespace Engine {

    struct OpenALEfxState {
        struct Object {
            AudioEfxType type = AudioEfxType::None;
            uint32 nativeId = 0;
            AudioEfxHandle handle;
            AudioReverbParameters reverb;
            AudioLowPassParameters lowpass;
            AudioEfxHandle attachedEffect; // slot -> effect
            AudioEfxHandle attachedSlot;   // reverb -> slot
        };

        struct Route {
            AudioEfxHandle slot;
            uint32 sendFilterNative = 0;
            float32 level = 1.0f;
        };

        mutable std::mutex mutex;
        std::unordered_map<uint32, Object> objects;
        std::unordered_map<uint32, Route> routes; // source native id
        std::unordered_map<uint32, AudioEfxHandle> directFilters; // source native id
        uint32 nextSerial = 1;
        AudioEfxCapabilities caps;
    };

    namespace {

        AudioEfxHandle MakeEfxHandle(const AudioEngineRuntime& runtime,
                                     AudioEfxType type,
                                     uint32 serial) {
            AudioEfxHandle handle;
            handle.engineId = runtime.instanceId;
            handle.generation = static_cast<uint32>(
                runtime.generation.load(std::memory_order_acquire));
            handle.serial = serial;
            handle.type = type;
            return handle;
        }

        bool IsFiniteInRange(float32 value, float32 minValue, float32 maxValue) {
            return std::isfinite(value) && value >= minValue && value <= maxValue;
        }

        bool ValidReverbParameters(const AudioReverbParameters& p) {
            return IsFiniteInRange(p.density, 0.0f, 1.0f) &&
                   IsFiniteInRange(p.diffusion, 0.0f, 1.0f) &&
                   IsFiniteInRange(p.gain, 0.0f, 1.0f) &&
                   IsFiniteInRange(p.gainHF, 0.0f, 1.0f) &&
                   IsFiniteInRange(p.decayTime, 0.1f, 20.0f) &&
                   IsFiniteInRange(p.decayHFRatio, 0.1f, 2.0f);
        }

        bool ValidLowPassParameters(const AudioLowPassParameters& p) {
            return IsFiniteInRange(p.gain, 0.0f, 1.0f) &&
                   IsFiniteInRange(p.gainHF, 0.0f, 1.0f);
        }

        bool NearlyEqual(float32 a, float32 b) {
            return std::fabs(a - b) <= 1e-4f;
        }

        bool ApplyReverb(IOpenALBackend& backend, uint32 effect,
                         const AudioReverbParameters& p) {
            if (!backend.SetEffectFloat(effect, AudioBackendEffectParam::Density,
                                        p.density) ||
                !backend.SetEffectFloat(effect, AudioBackendEffectParam::Diffusion,
                                        p.diffusion) ||
                !backend.SetEffectFloat(effect, AudioBackendEffectParam::Gain,
                                        p.gain) ||
                !backend.SetEffectFloat(effect, AudioBackendEffectParam::GainHF,
                                        p.gainHF) ||
                !backend.SetEffectFloat(effect, AudioBackendEffectParam::DecayTime,
                                        p.decayTime) ||
                !backend.SetEffectFloat(effect,
                                        AudioBackendEffectParam::DecayHFRatio,
                                        p.decayHFRatio)) {
                return false;
            }

            float32 value = 0.0f;
            return backend.GetEffectFloat(effect, AudioBackendEffectParam::Density,
                                          value) &&
                   NearlyEqual(value, p.density) &&
                   backend.GetEffectFloat(effect, AudioBackendEffectParam::Diffusion,
                                          value) &&
                   NearlyEqual(value, p.diffusion) &&
                   backend.GetEffectFloat(effect, AudioBackendEffectParam::Gain,
                                          value) &&
                   NearlyEqual(value, p.gain) &&
                   backend.GetEffectFloat(effect, AudioBackendEffectParam::GainHF,
                                          value) &&
                   NearlyEqual(value, p.gainHF) &&
                   backend.GetEffectFloat(effect, AudioBackendEffectParam::DecayTime,
                                          value) &&
                   NearlyEqual(value, p.decayTime) &&
                   backend.GetEffectFloat(effect,
                                          AudioBackendEffectParam::DecayHFRatio,
                                          value) &&
                   NearlyEqual(value, p.decayHFRatio);
        }

        bool ApplyLowPass(IOpenALBackend& backend, uint32 filter,
                          const AudioLowPassParameters& p) {
            if (!backend.SetFilterFloat(filter, AudioBackendFilterParam::Gain,
                                        p.gain) ||
                !backend.SetFilterFloat(filter, AudioBackendFilterParam::GainHF,
                                        p.gainHF)) {
                return false;
            }
            float32 value = 0.0f;
            return backend.GetFilterFloat(filter, AudioBackendFilterParam::Gain,
                                          value) &&
                   NearlyEqual(value, p.gain) &&
                   backend.GetFilterFloat(filter, AudioBackendFilterParam::GainHF,
                                          value) &&
                   NearlyEqual(value, p.gainHF);
        }

        AudioStatus ResolveEfxObject(
            OpenALEfxState& efx,
            const AudioEngineRuntime& runtime,
            const AudioEfxHandle& handle,
            AudioEfxType expectedType,
            OpenALEfxState::Object*& out) {
            out = nullptr;
            if (!handle.IsValid()) return AudioStatus::NotFound;
            if (handle.type != expectedType) return AudioStatus::InvalidArgument;
            if (handle.engineId != runtime.instanceId ||
                handle.generation != static_cast<uint32>(
                    runtime.generation.load(std::memory_order_acquire))) {
                return AudioStatus::NotFound;
            }
            auto it = efx.objects.find(handle.serial);
            if (it == efx.objects.end() || it->second.handle != handle) {
                return AudioStatus::NotFound;
            }
            out = &it->second;
            return AudioStatus::Ok;
        }

    } // namespace

    OpenALAudioEngine::OpenALAudioEngine()
        : OpenALAudioEngine(CreateOpenALBackend()) {
    }

    OpenALAudioEngine::OpenALAudioEngine(std::shared_ptr<IOpenALBackend> backend)
        : m_Runtime(std::make_shared<AudioEngineRuntime>(std::move(backend)))
        , m_Efx(std::make_unique<OpenALEfxState>()) {
        if (m_Runtime) {
            m_Runtime->onSourceRelease = [this](uint32 sourceId) {
                (void)CloseSourceEfx(sourceId, true);
            };
        }
    }

    OpenALAudioEngine::~OpenALAudioEngine() {
        if (!m_Runtime) return;
        const auto phase = m_Runtime->phase.load(std::memory_order_acquire);
        if (phase == AudioEngineState::Uninitialized ||
            phase == AudioEngineState::Shutdown) {
            return;
        }

        if (m_Runtime->IsOwnerThread()) {
            (void)Shutdown();
        } else {
            // Destruction itself cannot call AL on a non-owner thread.
            s_Log.Error("OpenALAudioEngine destroyed on a non-owner thread; "
                        "marking Shutdown without backend cleanup");
            m_Runtime->phase.store(AudioEngineState::Shutdown,
                                   std::memory_order_release);
        }
    }

    bool OpenALAudioEngine::Init() {
        if (!m_Runtime) return false;
        const auto phase = m_Runtime->phase.load(std::memory_order_acquire);

        if (phase == AudioEngineState::Ready) {
            if (!m_Runtime->IsOwnerThread()) {
                m_Runtime->SetLastStatus(AudioStatus::WrongThread);
                return false;
            }
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return true;
        }

        if (phase == AudioEngineState::ShuttingDown) {
            m_Runtime->SetLastStatus(AudioStatus::ShuttingDown);
            return false;
        }

        if (phase == AudioEngineState::Failed) {
            m_Runtime->SetLastStatus(m_Runtime->IsOwnerThread()
                                         ? AudioStatus::InvalidState
                                         : AudioStatus::WrongThread);
            return false;
        }

        // Uninitialized or Shutdown: bind this thread as the new owner.
        m_Runtime->ownerThread.store(std::this_thread::get_id(),
                                     std::memory_order_release);

        auto& backend = m_Runtime->backend;
        if (!backend) {
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::BackendError);
            return false;
        }

        if (!backend->OpenDevice()) {
            s_Log.Error("Failed to open default audio device");
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::BackendError);
            return false;
        }

        if (!backend->CreateContext()) {
            s_Log.Error("Failed to create audio context");
            backend->CloseDevice();
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::BackendError);
            return false;
        }

        if (!backend->MakeContextCurrent()) {
            s_Log.Error("Failed to make context current");
            backend->DestroyContext();
            backend->CloseDevice();
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::BackendError);
            return false;
        }

        const bool defaultsOk =
            backend->SetDistanceModel(AttenuationMode::InverseClamped) &&
            backend->SetListenerPosition(Vec3(0.0f, 0.0f, 0.0f)) &&
            backend->SetListenerOrientation(Vec3(0.0f, 0.0f, -1.0f),
                                            Vec3(0.0f, 1.0f, 0.0f)) &&
            backend->SetListenerGain(1.0f);

        if (!defaultsOk) {
            backend->ClearContextCurrent();
            backend->DestroyContext();
            backend->CloseDevice();
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::BackendError);
            return false;
        }

        m_Runtime->disconnectExtension.store(
            backend->IsExtensionPresent("ALC_EXT_disconnect"),
            std::memory_order_release);
        m_Runtime->generation.fetch_add(1, std::memory_order_acq_rel);
        m_Runtime->ClearRegistries();
        m_Runtime->ClearDeferred();
        m_Runtime->phase.store(AudioEngineState::Ready,
                               std::memory_order_release);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        InitializeEfxCapabilities();
        s_Log.Info("Initialized (owner thread bound)");
        return true;
    }

    AudioStatus OpenALAudioEngine::Shutdown() {
        if (!m_Runtime) return AudioStatus::NotInitialized;

        const auto phase = m_Runtime->phase.load(std::memory_order_acquire);
        if (phase == AudioEngineState::Uninitialized) {
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return AudioStatus::Ok;
        }
        if (phase == AudioEngineState::Shutdown) {
            if (!m_Runtime->IsOwnerThread()) {
                m_Runtime->SetLastStatus(AudioStatus::WrongThread);
                return AudioStatus::WrongThread;
            }
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return AudioStatus::Ok;
        }
        if (phase == AudioEngineState::ShuttingDown) {
            m_Runtime->SetLastStatus(AudioStatus::ShuttingDown);
            return AudioStatus::ShuttingDown;
        }
        if (!m_Runtime->IsOwnerThread()) {
            m_Runtime->SetLastStatus(AudioStatus::WrongThread);
            return AudioStatus::WrongThread;
        }

        const bool wasFailed = (phase == AudioEngineState::Failed);
        m_Runtime->phase.store(AudioEngineState::ShuttingDown,
                               std::memory_order_release);

        ShutdownEfx(!wasFailed);

        const auto sources = m_Runtime->SnapshotSources();
        const auto buffers = m_Runtime->SnapshotBuffers();

        if (!wasFailed && m_Runtime->backend) {
            for (const uint32 source : sources) {
                m_Runtime->backend->SourceStop(source);
                m_Runtime->backend->DeleteSource(source);
            }
            for (const uint32 buffer : buffers) {
                m_Runtime->backend->DeleteBuffer(buffer);
            }
            m_Runtime->DrainDeferred();
        } else {
            // Failed context: do not issue ordinary alDelete* calls.
            m_Runtime->ClearDeferred();
        }

        m_Runtime->ClearRegistries();
        if (m_Runtime->backend) {
            m_Runtime->backend->ClearContextCurrent();
            m_Runtime->backend->DestroyContext();
            m_Runtime->backend->CloseDevice();
        }

        m_Runtime->phase.store(AudioEngineState::Shutdown,
                               std::memory_order_release);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        s_Log.Info("Shut down");
        return AudioStatus::Ok;
    }

    bool OpenALAudioEngine::IsInitialized() const {
        return m_Runtime &&
               m_Runtime->phase.load(std::memory_order_acquire) ==
                   AudioEngineState::Ready;
    }

    AudioEngineState OpenALAudioEngine::GetState() const {
        return m_Runtime
            ? m_Runtime->phase.load(std::memory_order_acquire)
            : AudioEngineState::Uninitialized;
    }

    AudioStatus OpenALAudioEngine::GetLastStatus() const {
        return m_Runtime
            ? m_Runtime->lastStatus.load(std::memory_order_acquire)
            : AudioStatus::NotInitialized;
    }

    std::shared_ptr<IAudioBuffer> OpenALAudioEngine::CreateBuffer(
        const void* pcmData, int32 dataSize, const AudioClipInfo& info) {
        if (!m_Runtime) return nullptr;
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return nullptr;
        }

        const uint32 bufferID = m_Runtime->backend->GenBuffer();
        if (!bufferID) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return nullptr;
        }

        auto buffer = std::make_shared<OpenALAudioBuffer>(
            m_Runtime, bufferID, m_Runtime->generation.load(std::memory_order_acquire));

        const auto loadStatus = buffer->Load(pcmData, dataSize, info);
        if (loadStatus != AudioStatus::Ok) {
            buffer->Invalidate();
            m_Runtime->backend->DeleteBuffer(bufferID);
            m_Runtime->SetLastStatus(loadStatus);
            return nullptr;
        }

        m_Runtime->RegisterBuffer(bufferID);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return buffer;
    }

    AudioStatus OpenALAudioEngine::DestroyBuffer(IAudioBuffer* buffer) {
        if (!m_Runtime) return AudioStatus::NotInitialized;
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!buffer) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }
        if (!buffer->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        const uint32 bufferID = buffer->GetNativeHandle();
        if (!bufferID) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        if (m_Runtime->IsBufferBound(bufferID)) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidState);
            return AudioStatus::InvalidState;
        }
        if (!m_Runtime->UnregisterBuffer(bufferID)) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        buffer->Invalidate();
        if (!m_Runtime->backend->DeleteBuffer(bufferID)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    std::shared_ptr<IAudioSource> OpenALAudioEngine::CreateSource() {
        if (!m_Runtime) return nullptr;
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return nullptr;
        }

        const uint32 sourceID = m_Runtime->backend->GenSource();
        if (!sourceID) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return nullptr;
        }

        auto source = std::make_shared<OpenALAudioSource>(
            m_Runtime, sourceID, m_Runtime->generation.load(std::memory_order_acquire));
        m_Runtime->RegisterSource(sourceID);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return source;
    }

    AudioStatus OpenALAudioEngine::DestroySource(IAudioSource* source) {
        if (!m_Runtime) return AudioStatus::NotInitialized;
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!source) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }
        if (!source->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        const uint32 sourceID = source->GetNativeHandle();
        if (!sourceID || !m_Runtime->UnregisterSource(sourceID)) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        (void)CloseSourceEfx(sourceID, true);
        m_Runtime->backend->SourceStop(sourceID);
        m_Runtime->backend->SetSourceInt(
            sourceID, AudioBackendSourceParam::Buffer, 0);
        source->Invalidate();

        if (!m_Runtime->backend->DeleteSource(sourceID)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::SetListenerPosition(const Vec3& position) {
        const auto status = m_Runtime ? m_Runtime->CheckOwnerReady()
                                      : AudioStatus::NotInitialized;
        if (status != AudioStatus::Ok) {
            if (m_Runtime) m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!m_Runtime->backend->SetListenerPosition(position)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    Vec3 OpenALAudioEngine::GetListenerPosition() const {
        if (!m_Runtime) return Vec3(0.0f, 0.0f, 0.0f);
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        Vec3 position(0.0f, 0.0f, 0.0f);
        if (!m_Runtime->backend->GetListenerPosition(position)) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return position;
    }

    AudioStatus OpenALAudioEngine::SetListenerOrientation(
        const Vec3& forward, const Vec3& up) {
        const auto status = m_Runtime ? m_Runtime->CheckOwnerReady()
                                      : AudioStatus::NotInitialized;
        if (status != AudioStatus::Ok) {
            if (m_Runtime) m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!m_Runtime->backend->SetListenerOrientation(forward, up)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::SetListenerVolume(float32 gain) {
        return SetMasterVolume(gain);
    }

    AudioStatus OpenALAudioEngine::SetMasterVolume(float32 gain) {
        const auto status = m_Runtime ? m_Runtime->CheckOwnerReady()
                                      : AudioStatus::NotInitialized;
        if (status != AudioStatus::Ok) {
            if (m_Runtime) m_Runtime->SetLastStatus(status);
            return status;
        }
        const float32 clamped = std::clamp(gain, 0.0f, 1.0f);
        if (!m_Runtime->backend->SetListenerGain(clamped)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    float32 OpenALAudioEngine::GetMasterVolume() const {
        if (!m_Runtime) return 0.0f;
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return 0.0f;
        }
        float32 gain = 0.0f;
        if (!m_Runtime->backend->GetListenerGain(gain)) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return 0.0f;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return gain;
    }

    AudioStatus OpenALAudioEngine::SetDistanceModel(AttenuationMode mode) {
        const auto status = m_Runtime ? m_Runtime->CheckOwnerReady()
                                      : AudioStatus::NotInitialized;
        if (status != AudioStatus::Ok) {
            if (m_Runtime) m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!m_Runtime->backend->SetDistanceModel(mode)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::Update() {
        const auto status = m_Runtime ? m_Runtime->CheckOwnerReady()
                                      : AudioStatus::NotInitialized;
        if (status != AudioStatus::Ok) {
            if (m_Runtime) m_Runtime->SetLastStatus(status);
            return status;
        }

        if (m_Runtime->disconnectExtension.load(std::memory_order_acquire) &&
            !m_Runtime->backend->IsDeviceConnected()) {
            m_Runtime->phase.store(AudioEngineState::Failed,
                                   std::memory_order_release);
            m_Runtime->SetLastStatus(AudioStatus::DeviceLost);
            return AudioStatus::DeviceLost;
        }

        m_Runtime->DrainDeferred();
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    void* OpenALAudioEngine::GetNativeDevice() const {
        return m_Runtime && m_Runtime->backend
            ? m_Runtime->backend->GetDeviceHandle()
            : nullptr;
    }

    void* OpenALAudioEngine::GetNativeContext() const {
        return m_Runtime && m_Runtime->backend
            ? m_Runtime->backend->GetContextHandle()
            : nullptr;
    }

    AudioStatus OpenALAudioEngine::CheckEfxReady() const {
        if (!m_Runtime || !m_Efx) {
            return AudioStatus::NotInitialized;
        }
        const auto status = m_Runtime->CheckOwnerReady();
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        if (m_Efx->caps.status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(m_Efx->caps.status);
            return m_Efx->caps.status;
        }
        if (!m_Efx->caps.supported) {
            m_Runtime->SetLastStatus(AudioStatus::Unsupported);
            return AudioStatus::Unsupported;
        }
        return AudioStatus::Ok;
    }

    void OpenALAudioEngine::InitializeEfxCapabilities() {
        if (!m_Efx || !m_Runtime) return;

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        m_Efx->objects.clear();
        m_Efx->routes.clear();
        m_Efx->directFilters.clear();
        m_Efx->nextSerial = 1;
        m_Efx->caps = AudioEfxCapabilities{};

        auto& backend = m_Runtime->backend;
        if (!backend) {
            m_Efx->caps.status = AudioStatus::BackendError;
            return;
        }
        if (!backend->IsExtensionPresent("ALC_EXT_EFX")) {
            m_Efx->caps.status = AudioStatus::Unsupported;
            return;
        }
        if (!backend->AreEfxEntryPointsAvailable()) {
            m_Efx->caps.status = AudioStatus::BackendError;
            return;
        }

        const int32 maxSends = backend->GetMaxAuxiliarySends();
        m_Efx->caps.supported = true;
        m_Efx->caps.maxAuxiliarySends = maxSends;
        m_Efx->caps.routingSupported = maxSends > 0;
        m_Efx->caps.status = AudioStatus::Ok;
    }

    void OpenALAudioEngine::ShutdownEfx(bool backendAvailable) {
        if (!m_Efx) return;

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        IOpenALBackend* backend =
            (m_Runtime && m_Runtime->backend) ? m_Runtime->backend.get() : nullptr;

        if (backendAvailable && backend) {
            for (const auto& [sourceID, route] : m_Efx->routes) {
                backend->SetSourceAuxSend(sourceID, 0, 0, 0);
                if (route.sendFilterNative != 0) {
                    backend->DeleteFilter(route.sendFilterNative);
                }
            }
            m_Efx->routes.clear();

            for (const auto& [sourceID, filter] : m_Efx->directFilters) {
                (void)filter;
                backend->SetSourceDirectFilter(sourceID, 0);
            }
            m_Efx->directFilters.clear();

            std::vector<uint32> effectSerials;
            std::vector<uint32> slotSerials;
            for (const auto& [serial, object] : m_Efx->objects) {
                if (object.type == AudioEfxType::LowPassFilter) {
                    backend->DeleteFilter(object.nativeId);
                } else if (object.type == AudioEfxType::AuxiliarySlot) {
                    slotSerials.push_back(serial);
                } else if (object.type == AudioEfxType::Reverb) {
                    effectSerials.push_back(serial);
                }
            }

            for (const uint32 serial : slotSerials) {
                auto it = m_Efx->objects.find(serial);
                if (it != m_Efx->objects.end() &&
                    it->second.attachedEffect.IsValid()) {
                    backend->SetAuxiliarySlotInt(
                        it->second.nativeId, AudioBackendSlotParam::Effect, 0);
                }
            }
            for (const uint32 serial : effectSerials) {
                auto it = m_Efx->objects.find(serial);
                if (it != m_Efx->objects.end()) {
                    backend->DeleteEffect(it->second.nativeId);
                }
            }
            for (const uint32 serial : slotSerials) {
                auto it = m_Efx->objects.find(serial);
                if (it != m_Efx->objects.end()) {
                    backend->DeleteAuxiliarySlot(it->second.nativeId);
                }
            }
        }

        m_Efx->objects.clear();
        m_Efx->routes.clear();
        m_Efx->directFilters.clear();
        m_Efx->nextSerial = 1;
        m_Efx->caps = AudioEfxCapabilities{};
    }

    AudioStatus OpenALAudioEngine::CloseSourceEfx(uint32 sourceId,
                                                  bool backendAvailable) {
        if (!m_Efx) return AudioStatus::Ok;

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        IOpenALBackend* backend =
            (m_Runtime && m_Runtime->backend) ? m_Runtime->backend.get() : nullptr;
        AudioStatus result = AudioStatus::Ok;

        auto routeIt = m_Efx->routes.find(sourceId);
        if (routeIt != m_Efx->routes.end()) {
            if (backendAvailable && backend) {
                if (!backend->SetSourceAuxSend(sourceId, 0, 0, 0) ||
                    (routeIt->second.sendFilterNative != 0 &&
                     !backend->DeleteFilter(routeIt->second.sendFilterNative))) {
                    result = AudioStatus::BackendError;
                }
            }
            m_Efx->routes.erase(routeIt);
        }

        auto directIt = m_Efx->directFilters.find(sourceId);
        if (directIt != m_Efx->directFilters.end()) {
            if (backendAvailable && backend) {
                if (!backend->SetSourceDirectFilter(sourceId, 0)) {
                    result = AudioStatus::BackendError;
                }
            }
            m_Efx->directFilters.erase(directIt);
        }

        return result;
    }

    AudioEfxCapabilities OpenALAudioEngine::GetEfxCapabilities() const {
        if (!m_Runtime || !m_Efx) {
            return AudioEfxCapabilities{};
        }

        const auto phase = m_Runtime->phase.load(std::memory_order_acquire);
        AudioEfxCapabilities result;
        switch (phase) {
            case AudioEngineState::Ready:
                break;
            case AudioEngineState::Uninitialized:
            case AudioEngineState::Shutdown:
                result.status = AudioStatus::NotInitialized;
                return result;
            case AudioEngineState::Failed:
                result.status = AudioStatus::DeviceLost;
                return result;
            case AudioEngineState::ShuttingDown:
                result.status = AudioStatus::ShuttingDown;
                return result;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        return m_Efx->caps;
    }

    AudioEfxHandle OpenALAudioEngine::CreateReverb() {
        if (CheckEfxReady() != AudioStatus::Ok || !m_Runtime->backend) {
            return {};
        }

        auto& backend = *m_Runtime->backend;
        const uint32 nativeId = backend.GenEffect();
        if (!nativeId) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }
        if (!backend.SetEffectInt(
                nativeId, AudioBackendEffectParam::Type,
                static_cast<int32>(AudioBackendEffectType::Reverb))) {
            backend.DeleteEffect(nativeId);
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }

        const AudioReverbParameters defaults;
        if (!ApplyReverb(backend, nativeId, defaults)) {
            backend.DeleteEffect(nativeId);
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        uint32 serial = m_Efx->nextSerial++;
        if (serial == 0) serial = m_Efx->nextSerial++;
        const AudioEfxHandle handle =
            MakeEfxHandle(*m_Runtime, AudioEfxType::Reverb, serial);

        OpenALEfxState::Object object;
        object.type = AudioEfxType::Reverb;
        object.nativeId = nativeId;
        object.handle = handle;
        object.reverb = defaults;
        m_Efx->objects.emplace(serial, object);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return handle;
    }

    AudioStatus OpenALAudioEngine::SetReverbParameters(
        const AudioEfxHandle& effect, const AudioReverbParameters& params) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!ValidReverbParameters(params)) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, effect,
                                       AudioEfxType::Reverb, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        if (!ApplyReverb(*m_Runtime->backend, object->nativeId, params)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        object->reverb = params;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::GetReverbParameters(
        const AudioEfxHandle& effect, AudioReverbParameters& out) const {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        const auto status = ResolveEfxObject(*m_Efx, *m_Runtime, effect,
                                             AudioEfxType::Reverb, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        auto& backend = *m_Runtime->backend;
        AudioReverbParameters params;
        if (!backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::Density, params.density) ||
            !backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::Diffusion, params.diffusion) ||
            !backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::Gain, params.gain) ||
            !backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::GainHF, params.gainHF) ||
            !backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::DecayTime, params.decayTime) ||
            !backend.GetEffectFloat(object->nativeId,
                                    AudioBackendEffectParam::DecayHFRatio,
                                    params.decayHFRatio)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        out = params;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioEfxHandle OpenALAudioEngine::CreateAuxiliarySlot() {
        if (CheckEfxReady() != AudioStatus::Ok || !m_Runtime->backend) {
            return {};
        }

        const uint32 nativeId = m_Runtime->backend->GenAuxiliarySlot();
        if (!nativeId) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        uint32 serial = m_Efx->nextSerial++;
        if (serial == 0) serial = m_Efx->nextSerial++;
        const AudioEfxHandle handle =
            MakeEfxHandle(*m_Runtime, AudioEfxType::AuxiliarySlot, serial);

        OpenALEfxState::Object object;
        object.type = AudioEfxType::AuxiliarySlot;
        object.nativeId = nativeId;
        object.handle = handle;
        m_Efx->objects.emplace(serial, object);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return handle;
    }

    AudioStatus OpenALAudioEngine::AttachEffect(
        const AudioEfxHandle& slot, const AudioEfxHandle& effect) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* slotObject = nullptr;
        OpenALEfxState::Object* effectObject = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, slot,
                                       AudioEfxType::AuxiliarySlot, slotObject);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        status = ResolveEfxObject(*m_Efx, *m_Runtime, effect,
                                  AudioEfxType::Reverb, effectObject);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        if (slotObject->attachedEffect.IsValid() ||
            effectObject->attachedSlot.IsValid()) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidState);
            return AudioStatus::InvalidState;
        }

        if (!m_Runtime->backend->SetAuxiliarySlotInt(
                slotObject->nativeId, AudioBackendSlotParam::Effect,
                static_cast<int32>(effectObject->nativeId))) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }

        slotObject->attachedEffect = effect;
        effectObject->attachedSlot = slot;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::DetachEffect(const AudioEfxHandle& slot) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* slotObject = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, slot,
                                       AudioEfxType::AuxiliarySlot, slotObject);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        if (!slotObject->attachedEffect.IsValid()) {
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return AudioStatus::Ok;
        }

        if (!m_Runtime->backend->SetAuxiliarySlotInt(
                slotObject->nativeId, AudioBackendSlotParam::Effect, 0)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }

        const AudioEfxHandle effect = slotObject->attachedEffect;
        auto effectIt = m_Efx->objects.find(effect.serial);
        if (effectIt != m_Efx->objects.end() &&
            effectIt->second.handle == effect) {
            effectIt->second.attachedSlot = {};
        }
        slotObject->attachedEffect = {};
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::ConnectAuxSend(
        IAudioSource* source, const AudioEfxHandle& slot, float32 sendLevel) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!m_Efx->caps.routingSupported) {
            m_Runtime->SetLastStatus(AudioStatus::Unsupported);
            return AudioStatus::Unsupported;
        }
        if (!source || !source->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        const uint32 sourceID = source->GetNativeHandle();
        if (!sourceID) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        if (!std::isfinite(sendLevel)) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }
        const float32 level = std::clamp(sendLevel, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* slotObject = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, slot,
                                       AudioEfxType::AuxiliarySlot, slotObject);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        auto& backend = *m_Runtime->backend;
        auto existing = m_Efx->routes.find(sourceID);
        if (existing != m_Efx->routes.end()) {
            if (!backend.SetSourceAuxSend(sourceID, 0, 0, 0) ||
                (existing->second.sendFilterNative != 0 &&
                 !backend.DeleteFilter(existing->second.sendFilterNative))) {
                const auto failure = m_Runtime->ReportBackendFailure();
                m_Runtime->SetLastStatus(failure);
                return failure;
            }
            m_Efx->routes.erase(existing);
        }

        const uint32 sendFilter = backend.GenFilter();
        if (!sendFilter) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        if (!backend.SetFilterInt(sendFilter, AudioBackendFilterParam::Type,
                                  static_cast<int32>(AudioBackendFilterType::LowPass)) ||
            !backend.SetFilterFloat(sendFilter, AudioBackendFilterParam::Gain, level) ||
            !backend.SetFilterFloat(sendFilter, AudioBackendFilterParam::GainHF, 1.0f) ||
            !backend.SetSourceAuxSend(sourceID, 0, slotObject->nativeId, sendFilter)) {
            backend.DeleteFilter(sendFilter);
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }

        m_Efx->routes[sourceID] = { slot, sendFilter, level };
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::DisconnectAuxSend(IAudioSource* source) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!source || !source->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        const uint32 sourceID = source->GetNativeHandle();
        if (!sourceID) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        auto it = m_Efx->routes.find(sourceID);
        if (it == m_Efx->routes.end()) {
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return AudioStatus::Ok;
        }

        auto& backend = *m_Runtime->backend;
        if (!backend.SetSourceAuxSend(sourceID, 0, 0, 0) ||
            (it->second.sendFilterNative != 0 &&
             !backend.DeleteFilter(it->second.sendFilterNative))) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Efx->routes.erase(it);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioEfxHandle OpenALAudioEngine::CreateLowPassFilter() {
        if (CheckEfxReady() != AudioStatus::Ok || !m_Runtime->backend) {
            return {};
        }

        auto& backend = *m_Runtime->backend;
        const uint32 nativeId = backend.GenFilter();
        if (!nativeId) {
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }
        if (!backend.SetFilterInt(nativeId, AudioBackendFilterParam::Type,
                                  static_cast<int32>(AudioBackendFilterType::LowPass))) {
            backend.DeleteFilter(nativeId);
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }

        const AudioLowPassParameters defaults;
        if (!ApplyLowPass(backend, nativeId, defaults)) {
            backend.DeleteFilter(nativeId);
            m_Runtime->SetLastStatus(m_Runtime->ReportBackendFailure());
            return {};
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        uint32 serial = m_Efx->nextSerial++;
        if (serial == 0) serial = m_Efx->nextSerial++;
        const AudioEfxHandle handle =
            MakeEfxHandle(*m_Runtime, AudioEfxType::LowPassFilter, serial);

        OpenALEfxState::Object object;
        object.type = AudioEfxType::LowPassFilter;
        object.nativeId = nativeId;
        object.handle = handle;
        object.lowpass = defaults;
        m_Efx->objects.emplace(serial, object);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return handle;
    }

    AudioStatus OpenALAudioEngine::SetLowPassParameters(
        const AudioEfxHandle& filter, const AudioLowPassParameters& params) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!ValidLowPassParameters(params)) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, filter,
                                       AudioEfxType::LowPassFilter, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        if (!ApplyLowPass(*m_Runtime->backend, object->nativeId, params)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        object->lowpass = params;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::GetLowPassParameters(
        const AudioEfxHandle& filter, AudioLowPassParameters& out) const {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        const auto status = ResolveEfxObject(*m_Efx, *m_Runtime, filter,
                                             AudioEfxType::LowPassFilter, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        auto& backend = *m_Runtime->backend;
        AudioLowPassParameters params;
        if (!backend.GetFilterFloat(object->nativeId,
                                    AudioBackendFilterParam::Gain, params.gain) ||
            !backend.GetFilterFloat(object->nativeId,
                                    AudioBackendFilterParam::GainHF, params.gainHF)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        out = params;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::BindDirectFilter(
        IAudioSource* source, const AudioEfxHandle& filter) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!source || !source->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        const uint32 sourceID = source->GetNativeHandle();
        if (!sourceID) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, filter,
                                       AudioEfxType::LowPassFilter, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }
        if (m_Efx->directFilters.find(sourceID) != m_Efx->directFilters.end()) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidState);
            return AudioStatus::InvalidState;
        }

        if (!m_Runtime->backend->SetSourceDirectFilter(sourceID,
                                                       object->nativeId)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Efx->directFilters[sourceID] = filter;
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::UnbindDirectFilter(IAudioSource* source) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (!source || !source->IsOwnedBy(m_Runtime.get())) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }
        const uint32 sourceID = source->GetNativeHandle();
        if (!sourceID) {
            m_Runtime->SetLastStatus(AudioStatus::NotFound);
            return AudioStatus::NotFound;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        auto it = m_Efx->directFilters.find(sourceID);
        if (it == m_Efx->directFilters.end()) {
            m_Runtime->SetLastStatus(AudioStatus::Ok);
            return AudioStatus::Ok;
        }
        if (!m_Runtime->backend->SetSourceDirectFilter(sourceID, 0)) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Efx->directFilters.erase(it);
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioEngine::DestroyEfxObject(
        const AudioEfxHandle& handle) {
        if (CheckEfxReady() != AudioStatus::Ok) {
            return m_Runtime->GetLastStatus();
        }
        if (handle.type == AudioEfxType::None) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidArgument);
            return AudioStatus::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(m_Efx->mutex);
        OpenALEfxState::Object* object = nullptr;
        auto status = ResolveEfxObject(*m_Efx, *m_Runtime, handle,
                                       handle.type, object);
        if (status != AudioStatus::Ok) {
            m_Runtime->SetLastStatus(status);
            return status;
        }

        if (handle.type == AudioEfxType::Reverb &&
            object->attachedSlot.IsValid()) {
            m_Runtime->SetLastStatus(AudioStatus::InvalidState);
            return AudioStatus::InvalidState;
        }
        if (handle.type == AudioEfxType::AuxiliarySlot) {
            if (object->attachedEffect.IsValid()) {
                m_Runtime->SetLastStatus(AudioStatus::InvalidState);
                return AudioStatus::InvalidState;
            }
            for (const auto& [sourceID, route] : m_Efx->routes) {
                (void)sourceID;
                if (route.slot == handle) {
                    m_Runtime->SetLastStatus(AudioStatus::InvalidState);
                    return AudioStatus::InvalidState;
                }
            }
        }
        if (handle.type == AudioEfxType::LowPassFilter) {
            for (const auto& [sourceID, filter] : m_Efx->directFilters) {
                (void)sourceID;
                if (filter == handle) {
                    m_Runtime->SetLastStatus(AudioStatus::InvalidState);
                    return AudioStatus::InvalidState;
                }
            }
        }

        bool deleted = false;
        switch (handle.type) {
            case AudioEfxType::Reverb:
                deleted = m_Runtime->backend->DeleteEffect(object->nativeId);
                break;
            case AudioEfxType::AuxiliarySlot:
                deleted = m_Runtime->backend->DeleteAuxiliarySlot(object->nativeId);
                break;
            case AudioEfxType::LowPassFilter:
                deleted = m_Runtime->backend->DeleteFilter(object->nativeId);
                break;
            case AudioEfxType::None:
                break;
        }
        m_Efx->objects.erase(handle.serial);
        if (!deleted) {
            const auto failure = m_Runtime->ReportBackendFailure();
            m_Runtime->SetLastStatus(failure);
            return failure;
        }
        m_Runtime->SetLastStatus(AudioStatus::Ok);
        return AudioStatus::Ok;
    }

} // namespace Engine
