#pragma once

#include "ImportDiagnostic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// Shared URI/file-IO helpers (contract section 7). Same semantics as the
// glTF parser's embedded checks; kept as a free-standing layer so OBJ/MTL
// reuse the identical rules. glTF path refactor is deferred (B3+).
enum class UriStatus : uint8_t { Ok, DataUri, Scheme, Traversal, Percent, Invalid };

UriStatus ValidateUri(const std::string& uri);

void ReportUriFailure(std::vector<ImportDiagnostic>& diag,
                      const std::string& uri, const std::string& location);

// Reads an external asset file relative to sourceDir (same-directory
// relative URIs only; fail-closed). Reports the same diagnostics as the
// glTF parser (kCodeUri*, kCodeExternalBufferReadFail, limits).
bool ReadAssetFile(const std::string& sourceDir, const std::string& uri,
                   std::vector<uint8_t>* bytesOut,
                   std::vector<ImportDiagnostic>* diag);

} // namespace Engine::AssetImport
