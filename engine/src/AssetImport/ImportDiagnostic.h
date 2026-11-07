#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

enum class ImportSeverity : uint8_t { Error = 0, Warning = 1, Info = 2 };

struct ImportDiagnostic {
    ImportSeverity severity = ImportSeverity::Info;
    std::string code;      // stable machine code, e.g. "ACCESSOR_BOUNDS"
    std::string location;  // JSON pointer-ish path or container element id
    std::string message;   // human-readable detail
};

inline const char* ToString(ImportSeverity s) {
    switch (s) {
        case ImportSeverity::Error: return "ERROR";
        case ImportSeverity::Warning: return "WARNING";
        default: return "INFO";
    }
}

// Stable diagnostic codes (English, ASCII-only; asserted in tests).
inline constexpr const char* kCodeBadMagic = "GLB_BAD_MAGIC";
inline constexpr const char* kCodeBadVersion = "GLTF_BAD_VERSION";
inline constexpr const char* kCodeLengthMismatch = "GLB_LENGTH_MISMATCH";
inline constexpr const char* kCodeChunkUnaligned = "GLB_CHUNK_UNALIGNED";
inline constexpr const char* kCodeChunkBounds = "GLB_CHUNK_BOUNDS";
inline constexpr const char* kCodeChunkOrder = "GLB_CHUNK_ORDER";
inline constexpr const char* kCodeExtraChunks = "GLB_EXTRA_CHUNKS";
inline constexpr const char* kCodeJsonParse = "GLTF_JSON_PARSE";
inline constexpr const char* kCodeMissingField = "GLTF_FIELD_MISSING";
inline constexpr const char* kCodeAccessorBounds = "ACCESSOR_BOUNDS";
inline constexpr const char* kCodeBufferViewBounds = "BUFFERVIEW_BOUNDS";
inline constexpr const char* kCodeBadAlignment = "BUFFERVIEW_BAD_ALIGNMENT";
inline constexpr const char* kCodeInterleavedUnsupported = "INTERLEAVED_UNSUPPORTED_V1";
inline constexpr const char* kCodeAttrComponentUnsupported = "ATTR_COMPONENT_UNSUPPORTED";
inline constexpr const char* kCodeAttrNormalizedUnsupported = "ATTR_NORMALIZED_UNSUPPORTED_V1";
inline constexpr const char* kCodePrimitiveModeUnsupported = "PRIMITIVE_MODE_UNSUPPORTED";
inline constexpr const char* kCodeRequiredExtensionUnsupported = "REQUIRED_EXTENSION_UNSUPPORTED";
inline constexpr const char* kCodeDataUriUnsupported = "DATA_URI_UNSUPPORTED_V1";
inline constexpr const char* kCodeUriTraversal = "URI_PATH_TRAVERSAL";
inline constexpr const char* kCodeUriSchemeUnsupported = "URI_SCHEME_UNSUPPORTED";
inline constexpr const char* kCodeUriPercentUnsupported = "URI_PERCENT_UNSUPPORTED_V1";
inline constexpr const char* kCodeExternalBufferReadFail = "EXTERNAL_BUFFER_READ_FAIL";
inline constexpr const char* kCodeBufferNoUri = "GLTF_BUFFER_NO_URI";
inline constexpr const char* kCodeSparseUnsupported = "SPARSE_ACCESSOR_UNSUPPORTED_V1";
inline constexpr const char* kCodeLimitNodeDepth = "LIMIT_NODE_DEPTH";
inline constexpr const char* kCodeLimitPrimitives = "LIMIT_PRIMITIVES";
inline constexpr const char* kCodeLimitMeshes = "LIMIT_MESHES";
inline constexpr const char* kCodeLimitImages = "LIMIT_IMAGES";
inline constexpr const char* kCodeLimitBuffers = "LIMIT_BUFFERS";
inline constexpr const char* kCodeLimitVertices = "LIMIT_TOTAL_VERTICES";
inline constexpr const char* kCodeLimitSourceSize = "LIMIT_SOURCE_BYTES";
inline constexpr const char* kCodeMaterialRefMissing = "MATERIAL_REF_MISSING";
inline constexpr const char* kCodeAttributeMissing = "PRIMITIVE_POSITION_MISSING";
inline constexpr const char* kCodeAttributeCountMismatch = "PRIMITIVE_ATTR_COUNT_MISMATCH";
inline constexpr const char* kCodeNodeMeshMissing = "NODE_MESH_MISSING";
inline constexpr const char* kCodeTexImageMissing = "IMAGE_MISSING";
inline constexpr const char* kCodeMaterialUnspecified = "MATERIAL_UNSPECIFIED";
inline constexpr const char* kCodeTexCoordSetUnsupported = "TEXCOORD_SET_UNSUPPORTED";
inline constexpr const char* kCodeGlbBufferUriConflict = "GLB_BUFFER_URI_CONFLICT";
inline constexpr const char* kCodeNodeSkipped = "NODE_SKIPPED";
inline constexpr const char* kCodeExtensionIgnored = "EXTENSION_IGNORED";
inline constexpr const char* kCodeVertexColorDropped = "VERTEX_COLOR_DROPPED";
inline constexpr const char* kCodeSceneIgnored = "SCENE_IGNORED";
inline constexpr const char* kCodeUriInvalid = "URI_INVALID";
inline constexpr const char* kCodeTangentW = "TANGENT_W_UNUSUAL";
inline constexpr const char* kCodeValueInvalid = "GLTF_VALUE_INVALID";
inline constexpr const char* kCodeNormalDegenerate = "NORMAL_DEGENERATE";
inline constexpr const char* kCodeObjSyntax = "OBJ_SYNTAX";
inline constexpr const char* kCodeObjIndexOutOfRange = "OBJ_INDEX_OUT_OF_RANGE";
inline constexpr const char* kCodeObjFaceDegenerate = "OBJ_FACE_DEGENERATE";
inline constexpr const char* kCodeObjUnknownDirective = "OBJ_UNKNOWN_DIRECTIVE";
inline constexpr const char* kCodeObjSmoothIgnored = "OBJ_SMOOTH_IGNORED";
inline constexpr const char* kCodeObjPolygonTriangulated = "OBJ_POLYGON_TRIANGULATED";
inline constexpr const char* kCodeObjFacesMissing = "OBJ_NO_FACES";
inline constexpr const char* kCodeMtlSyntax = "MTL_SYNTAX";
inline constexpr const char* kCodeMtlUnknownDirective = "MTL_UNKNOWN_DIRECTIVE";
inline constexpr const char* kCodeMtlFieldIgnored = "MTL_FIELD_IGNORED";
inline constexpr const char* kCodeMtlLibMissing = "MTL_LIB_MISSING";
inline constexpr const char* kCodeMtlMaterialMissing = "MTL_MATERIAL_MISSING";
inline constexpr const char* kCodeMtlDuplicateMaterial = "MTL_DUPLICATE_MATERIAL";
inline constexpr const char* kCodeLimitMaterials = "LIMIT_MATERIALS";
inline constexpr const char* kCodeTextureDecodeFail = "TEXTURE_DECODE_FAIL";
inline constexpr const char* kCodeTextureInvalidDims = "TEXTURE_INVALID_DIMENSIONS";
inline constexpr const char* kCodeTextureDimsExceedLimit = "TEXTURE_DIMS_EXCEED_LIMIT";
inline constexpr const char* kCodeTexturePixelsExceedLimit = "TEXTURE_PIXELS_EXCEED_LIMIT";
inline constexpr const char* kCodeTextureSourceMissing = "TEXTURE_SOURCE_MISSING";
inline constexpr const char* kCodeMaterialRefUnresolved = "MATERIAL_TEXTURE_UNRESOLVED";
inline constexpr const char* kCodeMeshAssetParse = "MESH_ASSET_PARSE_FAIL";
inline constexpr const char* kCodeMeshAssetUnsupportedVersion = "MESH_ASSET_UNSUPPORTED_VERSION";
inline constexpr const char* kCodeMeshAssetInvalid = "MESH_ASSET_INVALID";
inline constexpr const char* kCodeMaterialAssetParse = "MATERIAL_ASSET_PARSE_FAIL";
inline constexpr const char* kCodeMaterialAssetUnsupportedVersion = "MATERIAL_ASSET_UNSUPPORTED_VERSION";
inline constexpr const char* kCodeMaterialAssetInvalid = "MATERIAL_ASSET_INVALID";
inline constexpr const char* kCodeTextureAssetParse = "TEXTURE_ASSET_PARSE_FAIL";
inline constexpr const char* kCodeTextureAssetInvalid = "TEXTURE_ASSET_INVALID";
inline constexpr const char* kCodeTextureAssetUnsupportedVersion = "TEXTURE_ASSET_UNSUPPORTED_VERSION";
inline constexpr const char* kCodeMaterialBindingMissing = "MATERIAL_BINDING_MISSING";
inline constexpr const char* kCodeMaterialBindingInvalid = "MATERIAL_BINDING_INVALID";

} // namespace Engine::AssetImport
