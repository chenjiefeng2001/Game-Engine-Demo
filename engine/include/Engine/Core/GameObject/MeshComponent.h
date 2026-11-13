#pragma once

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace Engine {

    class MeshRuntimeScene;

    // P6-1B B5-G4: runtime-only load state, never serialized. Asset identity is
    // the persisted GUID; status/error/revision are rebuildable runtime facts.
    enum class MeshRuntimeStatus : uint8_t {
        NotRequested = 0,
        Unloaded,
        Ready,
        MissingAsset,
        InvalidAsset,
        UnsupportedAssetType,
        ReaderFailure,
        GPUUploadFailure,
        MaterialBindingFailure,
        TextureBindingFailure,
        NotCommitted
    };

    /**
     * @brief 网格组件 — 挂载 3D 网格到 GameObject 上
     *
     * 配合 MeshRenderer 使用。每个 MeshComponent 可以携带一个 Mesh
     * 引用和简单的材质属性。
     */
    class MeshComponent : public Component {
    public:
        MeshComponent() = default;
        explicit MeshComponent(std::shared_ptr<Mesh> mesh)
            : m_Mesh(std::move(mesh)) {}

        // ── 网格 ──
        void SetMesh(std::shared_ptr<Mesh> mesh) { m_Mesh = std::move(mesh); }
        std::shared_ptr<Mesh> GetMesh() const { return m_Mesh; }
        bool HasMesh() const { return m_Mesh && m_Mesh->IsValid(); }

        // P6-1B B5-G4: persistent asset identity (Scene-persisted).
        void SetMeshGuid(const ResourceGUID& guid) { m_MeshGuid = guid; }
        const ResourceGUID& GetMeshGuid() const { return m_MeshGuid; }
        bool HasMeshGuid() const { return !m_MeshGuid.IsNull(); }
        static uint64_t FoldGuid(const ResourceGUID& guid) {
            if (guid.IsNull()) return 0;
            uint64_t h = guid.high * 0x9E3779B97F4A7C15ull;
            h ^= guid.low + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        }
        uint64_t GetAssetIdentity() const { return FoldGuid(m_MeshGuid); }

        // Runtime-only status (never serialized).
        MeshRuntimeStatus GetRuntimeStatus() const { return m_RuntimeStatus; }
        const std::string& GetRuntimeError() const { return m_RuntimeError; }
        int64_t GetResolvedRevision() const { return m_ResolvedRevision; }
        const std::string& GetResolvedAssetPath() const { return m_ResolvedAssetPath; }
        bool IsRuntimeReady() const { return m_RuntimeStatus == MeshRuntimeStatus::Ready; }

        static const char* RuntimeStatusToString(MeshRuntimeStatus status) {
            switch (status) {
            case MeshRuntimeStatus::NotRequested: return "NotRequested";
            case MeshRuntimeStatus::Unloaded: return "Unloaded";
            case MeshRuntimeStatus::Ready: return "Ready";
            case MeshRuntimeStatus::MissingAsset: return "MissingAsset";
            case MeshRuntimeStatus::InvalidAsset: return "InvalidAsset";
            case MeshRuntimeStatus::UnsupportedAssetType: return "UnsupportedAssetType";
            case MeshRuntimeStatus::ReaderFailure: return "ReaderFailure";
            case MeshRuntimeStatus::GPUUploadFailure: return "GPUUploadFailure";
            case MeshRuntimeStatus::MaterialBindingFailure: return "MaterialBindingFailure";
            case MeshRuntimeStatus::TextureBindingFailure: return "TextureBindingFailure";
            case MeshRuntimeStatus::NotCommitted: return "NotCommitted";
            }
            return "Unknown";
        }

        // B6 component-contract properties (Inspector/read-only diagnostics).
        size_t GetPropertyCount() const override { return 10; }
        bool GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const override {
            if (!out) return false;
            switch (index) {
            case 0: *out = {"meshGuid", ComponentValueType::String, true}; return true;
            case 1: *out = {"meshName", ComponentValueType::String, false}; return true;
            case 2: *out = {"spanCount", ComponentValueType::Int, false}; return true;
            case 3: *out = {"materialSlotCount", ComponentValueType::Int, false}; return true;
            case 4: *out = {"runtimeStatus", ComponentValueType::String, false}; return true;
            case 5: *out = {"runtimeError", ComponentValueType::String, false}; return true;
            case 6: *out = {"resolvedRevision", ComponentValueType::Int, false}; return true;
            case 7: *out = {"resolvedAssetPath", ComponentValueType::String, false}; return true;
            case 8: *out = {"visible", ComponentValueType::Bool, true}; return true;
            case 9: *out = {"runtimeWarnings", ComponentValueType::String, false}; return true;
            default: return false;
            }
        }
        bool GetPropertyValue(size_t index, ComponentPropertyValue* out) const override {
            if (!out) return false;
            switch (index) {
            case 0: out->type = ComponentValueType::String; out->stringValue = m_MeshGuid.ToHex(); return true;
            case 1: out->type = ComponentValueType::String; out->stringValue = MeshName(); return true;
            case 2: out->type = ComponentValueType::Int; out->intValue = static_cast<int32>(m_SpanCount); return true;
            case 3: out->type = ComponentValueType::Int; out->intValue = static_cast<int32>(m_MaterialSlotCount); return true;
            case 4: out->type = ComponentValueType::String; out->stringValue = RuntimeStatusToString(m_RuntimeStatus); return true;
            case 5: out->type = ComponentValueType::String; out->stringValue = m_RuntimeError; return true;
            case 6: out->type = ComponentValueType::Int; out->intValue = static_cast<int32>(m_ResolvedRevision); return true;
            case 7: out->type = ComponentValueType::String; out->stringValue = m_ResolvedAssetPath; return true;
            case 8: out->type = ComponentValueType::Bool; out->boolValue = m_Visible; return true;
            case 9: out->type = ComponentValueType::String; out->stringValue = m_RuntimeWarnings; return true;
            default: return false;
            }
        }
        bool SetPropertyValue(size_t index, const ComponentPropertyValue& value) override {
            if (index == 0 && value.type == ComponentValueType::String) {
                if (value.stringValue.empty()) { m_MeshGuid = ResourceGUID::Null; return true; }
                if (value.stringValue.size() != 32) return false;
                const ResourceGUID parsed = ResourceGUID::FromHex(value.stringValue);
                if (parsed.IsNull()) return false;
                m_MeshGuid = parsed;
                return true;
            }
            if (index == 8 && value.type == ComponentValueType::Bool) { m_Visible = value.boolValue; return true; }
            return false;
        }

        const char* GetComponentTypeName() const override { return "Mesh"; }

        void Serialize(nlohmann::json& json) const override {
            if (!m_MeshGuid.IsNull()) json["meshGuid"] = m_MeshGuid.ToHex();
        }

        bool Deserialize(const nlohmann::json& json) override {
            m_MeshGuid = ResourceGUID::Null;
            if (!json.contains("meshGuid")) return true;
            if (!json["meshGuid"].is_string()) return false;
            const std::string hex = json["meshGuid"].get<std::string>();
            if (hex.size() != 32) return false;
            for (const char c : hex) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (!std::isxdigit(u)) return false;
            }
            const ResourceGUID parsed = ResourceGUID::FromHex(hex);
            if (parsed.IsNull()) {
                const bool explicitNull =
                    hex.find_first_not_of('0') == std::string::npos;
                return explicitNull;
            }
            m_MeshGuid = parsed;
            return true;
        }

        // ── 材质属性（简化版，未来可拆为 Material 类） ──
        Vec4 m_Color      = {1.0f, 1.0f, 1.0f, 1.0f};
        bool m_Visible    = true;

        // ── 贴图资源 ──
        std::shared_ptr<class Texture> m_NormalMap;     // 法线贴图
        std::shared_ptr<class Texture> m_HeightMap;     // 高度贴图（视差/位移用）
        float m_NormalStrength = 1.0f;                  // 法线强度
        float m_ParallaxScale = 0.05f;                  // 视差缩放系数
        float m_DisplacementStrength = 0.0f;            // 位移强度（0=关闭）

        // ── 材质属性 ──
        float m_Shininess = 32.0f;
        float m_SpecularStrength = 0.5f;

        // 将来可以扩展：
        // std::shared_ptr<Texture> m_DiffuseTexture;
        // std::shared_ptr<Texture> m_NormalMap;
        // float m_Metallic = 0.0f;
        // float m_Roughness = 0.5f;

    private:
        friend class MeshRuntimeScene;

        std::string MeshName() const {
            if (m_ResolvedAssetPath.empty()) return {};
            const size_t slash = m_ResolvedAssetPath.find_last_of("/\\");
            return slash == std::string::npos ? m_ResolvedAssetPath
                                          : m_ResolvedAssetPath.substr(slash + 1);
        }

        void SetRuntimeReady(const ResourceGUID& guid, int64_t revision,
                             std::string assetPath,
                             std::shared_ptr<Mesh> mesh, uint32_t spanCount = 0,
                             uint32_t materialSlotCount = 0,
                             std::string warnings = {}) {
            m_RuntimeStatus = MeshRuntimeStatus::Ready;
            m_RuntimeError.clear();
            m_ResolvedRevision = revision;
            m_ResolvedAssetPath = std::move(assetPath);
            m_ResolvedGuid = guid;
            m_SpanCount = spanCount;
            m_MaterialSlotCount = materialSlotCount;
            m_RuntimeWarnings = std::move(warnings);
            m_Mesh = std::move(mesh);
        }

        void SetRuntimeFailure(const ResourceGUID& requestedGuid,
                               MeshRuntimeStatus status, std::string error,
                               int64_t revision) {
            if (m_ResolvedGuid != requestedGuid) {
                m_Mesh.reset();
                m_ResolvedGuid = ResourceGUID::Null;
                m_ResolvedAssetPath.clear();
            }
            m_RuntimeStatus = status;
            m_RuntimeError = std::move(error);
            m_RuntimeWarnings.clear();
            m_ResolvedRevision = revision;
        }

        void SetRuntimeUnbound() {
            m_RuntimeStatus = MeshRuntimeStatus::NotRequested;
            m_RuntimeError.clear();
            m_RuntimeWarnings.clear();
            m_ResolvedRevision = -1;
            m_ResolvedAssetPath.clear();
            m_ResolvedGuid = ResourceGUID::Null;
            m_Mesh.reset();
        }

        ResourceGUID m_MeshGuid = ResourceGUID::Null;
        ResourceGUID m_ResolvedGuid = ResourceGUID::Null;
        MeshRuntimeStatus m_RuntimeStatus = MeshRuntimeStatus::NotRequested;
        std::string m_RuntimeError;
        std::string m_RuntimeWarnings;
        int64_t m_ResolvedRevision = -1;
        uint32_t m_SpanCount = 0;
        uint32_t m_MaterialSlotCount = 0;
        std::string m_ResolvedAssetPath;
        std::shared_ptr<Mesh> m_Mesh;
    };

} // namespace Engine
