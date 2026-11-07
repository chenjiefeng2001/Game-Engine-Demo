#include "AssetFileReader.h"

#include "ImportLimits.h"

#include <fstream>

namespace Engine::AssetImport {

UriStatus ValidateUri(const std::string& uri) {
    if (uri.empty()) return UriStatus::Invalid;
    if (uri.rfind("data:", 0) == 0) return UriStatus::DataUri;
    if (uri.front() == '/' || uri.front() == '\\') return UriStatus::Traversal;
    for (const char c : uri) {
        if (c == '%') return UriStatus::Percent;
        if (c == '\\') return UriStatus::Traversal;
        if (c == ':') return UriStatus::Scheme;
        if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || c == 0) return UriStatus::Invalid;
    }
    size_t start = 0;
    while (true) {
        const size_t sep = uri.find('/', start);
        const size_t end = (sep == std::string::npos) ? uri.size() : sep;
        if (end - start == 2 && uri.compare(start, 2, "..") == 0)
            return UriStatus::Traversal;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return UriStatus::Ok;
}

void ReportUriFailure(std::vector<ImportDiagnostic>& diag,
                      const std::string& uri, const std::string& location) {
    auto push = [&](ImportSeverity sev, const char* code,
                    const std::string& message) {
        ImportDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.location = location;
        d.message = message;
        diag.push_back(std::move(d));
    };
    switch (ValidateUri(uri)) {
        case UriStatus::Ok:
            break;
        case UriStatus::DataUri:
            push(ImportSeverity::Error, kCodeDataUriUnsupported,
                 "data: URIs unsupported in v1: " + uri);
            break;
        case UriStatus::Scheme:
            push(ImportSeverity::Error, kCodeUriSchemeUnsupported,
                 "absolute or scheme URIs are rejected: " + uri);
            break;
        case UriStatus::Traversal:
            push(ImportSeverity::Error, kCodeUriTraversal,
                 "path traversal / backslash URIs are rejected: " + uri);
            break;
        case UriStatus::Percent:
            push(ImportSeverity::Error, kCodeUriPercentUnsupported,
                 "percent-encoded URIs unsupported in v1: " + uri);
            break;
        case UriStatus::Invalid:
        default:
            push(ImportSeverity::Error, kCodeUriInvalid,
                 "empty or invalid characters in URI: " + uri);
            break;
    }
}

bool ReadAssetFile(const std::string& sourceDir, const std::string& uri,
                   std::vector<uint8_t>* bytesOut,
                   std::vector<ImportDiagnostic>* diag) {
    const std::string loc = "uri=" + uri;
    ReportUriFailure(*diag, uri, loc);
    for (const auto& d : *diag) {
        if (d.severity == ImportSeverity::Error) return false;
    }
    std::string dir = sourceDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::string path = dir.empty() ? uri : (dir + "/" + uri);
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeExternalBufferReadFail;
        d.location = loc;
        d.message = "cannot open external file: " + path;
        diag->push_back(std::move(d));
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeExternalBufferReadFail;
        d.location = loc;
        d.message = "cannot determine file size: " + path;
        diag->push_back(std::move(d));
        return false;
    }
    if (static_cast<size_t>(size) > kMaxSourceFileBytes) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeLimitSourceSize;
        d.location = loc;
        d.message = "external file exceeds kMaxSourceFileBytes";
        diag->push_back(std::move(d));
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytesOut->resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytesOut->data()), size);
        if (in.gcount() != size) {
            ImportDiagnostic d;
            d.severity = ImportSeverity::Error;
            d.code = kCodeExternalBufferReadFail;
            d.location = loc;
            d.message = "short read: " + path;
            diag->push_back(std::move(d));
            return false;
        }
    }
    return true;
}

} // namespace Engine::AssetImport
