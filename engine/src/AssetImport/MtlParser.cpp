#include "MtlParser.h"

#include "AssetFileReader.h"
#include "ImportLimits.h"

#include <charconv>

namespace Engine::AssetImport {

namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

void AddDiag(std::vector<ImportDiagnostic>& d, ImportSeverity sev,
             const char* code, uint32_t line, const std::string& message) {
    ImportDiagnostic x;
    x.severity = sev;
    x.code = code;
    x.location = "line " + std::to_string(line);
    x.message = message;
    d.push_back(std::move(x));
}

bool HasError(const std::vector<ImportDiagnostic>& d) {
    for (const auto& x : d) {
        if (x.severity == ImportSeverity::Error) return true;
    }
    return false;
}

bool ParseFloat(const char* s, const char* e, float* out) {
    const auto r = std::from_chars(s, e, *out);
    return r.ec == std::errc();
}

void SplitTokens(const std::string& line, std::vector<std::string>* out) {
    out->clear();
    size_t i = 0;
    while (true) {
        i = line.find_first_not_of(" \t\r\n\f\v", i);
        if (i == std::string::npos) break;
        const size_t end = line.find_first_of(" \t\r\n\f\v", i);
        if (end == std::string::npos) {
            out->push_back(line.substr(i));
            break;
        }
        out->push_back(line.substr(i, end - i));
        i = end;
    }
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

// Extracts the first URI token from a map_* line, skipping "-option value"
// pairs (e.g. "bump -imfchan z noise.png").
bool ExtractTextureUri(const std::vector<std::string>& toks, std::string* uri) {
    for (size_t k = 1; k < toks.size(); ++k) {
        const std::string& t = toks[k];
        if (t.size() > 1 && t[0] == '-') {
            ++k;
            continue;
        }
        *uri = t;
        return true;
    }
    return false;
}

bool TextureUriAcceptable(std::vector<ImportDiagnostic>& diag, uint32_t line,
                          const std::string& uri) {
    ReportUriFailure(diag, uri, "line " + std::to_string(line));
    for (const auto& d : diag) {
        if (d.severity == ImportSeverity::Error) return false;
    }
    return true;
}

bool AttachImage(MtlParseResult& r, uint32_t line, const std::string& uri,
                 TextureReferenceIR* ref) {
    if (!TextureUriAcceptable(r.diagnostics, line, uri)) return false;
    for (uint32_t k = 0; k < r.images.size(); ++k) {
        if (r.images[k].uri == uri) {
            ref->imageIndex = k;
            ref->present = true;
            return true;
        }
    }
    if (r.images.size() >= kMaxImages) {
        AddDiag(r.diagnostics, ImportSeverity::Error, kCodeLimitImages, line,
                "referenced texture image count exceeds kMaxImages");
        return false;
    }
    TextureImageIR img;
    img.uri = uri;
    img.sourceImageIndex = static_cast<uint32_t>(r.images.size());
    r.images.push_back(std::move(img));
    ref->imageIndex = static_cast<uint32_t>(r.images.size()) - 1;
    ref->present = true;
    return true;
}

} // namespace

MtlParseResult MtlParser::ParseMtl(std::vector<uint8_t> bytes) {
    MtlParseResult r;
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    int32_t activeMaterial = -1;
    uint32_t lineNo = 0;
    std::vector<std::string> seenIgnored;
    std::vector<std::string> seenUnknown;
    std::vector<std::string> seenDuplicate;

    auto materialExistsInFile = [&r](const std::string& name) {
        if (name.empty()) return false;
        for (const auto& m : r.materials) {
            if (m.name == name) return true;
        }
        return false;
    };

    size_t pos = 0;
    while (pos < text.size()) {
        if (HasError(r.diagnostics)) break;
        const size_t eol = text.find('\n', pos);
        std::string raw;
        if (eol == std::string::npos) {
            raw = text.substr(pos);
            pos = text.size();
        } else {
            raw = text.substr(pos, eol - pos);
            pos = eol + 1;
        }
        ++lineNo;
        std::vector<std::string> toks;
        SplitTokens(raw, &toks);
        if (toks.empty() || toks[0].rfind('#', 0) == 0) continue;
        const std::string& key = toks[0];

        if (key == "newmtl") {
            if (toks.size() < 2) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, "newmtl requires a material name");
                continue;
            }
            const std::string name = toks[1];
            if (materialExistsInFile(name)) {
                if (!Contains(seenDuplicate, name)) {
                    seenDuplicate.push_back(name);
                    AddDiag(r.diagnostics, ImportSeverity::Warning,
                            kCodeMtlDuplicateMaterial, lineNo,
                            "duplicate material keeps first definition: " + name);
                }
                for (size_t i = 0; i < r.materials.size(); ++i) {
                    if (r.materials[i].name == name)
                        activeMaterial = static_cast<int32_t>(i);
                }
            } else {
                if (r.materials.size() >= kMaxMaterials) {
                    AddDiag(r.diagnostics, ImportSeverity::Error,
                            kCodeLimitMaterials, lineNo,
                            "material count exceeds kMaxMaterials");
                    continue;
                }
                MaterialIR m;
                m.name = name;
                r.materials.push_back(std::move(m));
                activeMaterial = static_cast<int32_t>(r.materials.size()) - 1;
            }
        } else if (key == "Kd" || key == "Ke" || key == "Ks") {
            if (activeMaterial < 0) {
                AddDiag(r.diagnostics, ImportSeverity::Warning,
                        kCodeMtlFieldIgnored, lineNo, key + " before any newmtl; ignored");
                continue;
            }
            std::vector<float> comps;
            bool numeric = true;
            for (size_t i = 1; i < toks.size(); ++i) {
                float f = 0.f;
                if (!ParseFloat(toks[i].c_str(), toks[i].c_str() + toks[i].size(), &f)) {
                    numeric = false;
                    break;
                }
                comps.push_back(f);
            }
            if (!numeric || comps.size() < 3) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, key + " requires 3 numeric components");
                continue;
            }
            if (key == "Kd") {
                for (int c = 0; c < 3; ++c)
                    r.materials[activeMaterial].baseColorFactor[c] = comps[c];
                if (comps.size() > 3)
                    r.materials[activeMaterial].baseColorFactor[3] = comps[3];
            } else if (key == "Ke") {
                for (int c = 0; c < 3; ++c)
                    r.materials[activeMaterial].emissiveFactor[c] = comps[c];
            } else {
                if (!Contains(seenIgnored, key)) {
                    seenIgnored.push_back(key);
                    AddDiag(r.diagnostics, ImportSeverity::Warning,
                            kCodeMtlFieldIgnored, lineNo,
                            "Ks stored but unused in v1 material model");
                }
            }
        } else if (key == "d" || key == "Tr") {
            if (activeMaterial < 0) {
                AddDiag(r.diagnostics, ImportSeverity::Warning,
                        kCodeMtlFieldIgnored, lineNo, key + " before any newmtl");
                continue;
            }
            float v = 0.f;
            if (toks.size() < 2 ||
                !ParseFloat(toks[1].c_str(), toks[1].c_str() + toks[1].size(), &v)) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, key + " requires a numeric value");
                continue;
            }
            if (key == "Tr") v = 1.0f - v;
            r.materials[activeMaterial].baseColorFactor[3] = v;
        } else if (key == "map_Kd" || key == "map_Ke") {
            if (activeMaterial < 0) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, key + " before any newmtl");
                continue;
            }
            std::string uri;
            if (!ExtractTextureUri(toks, &uri)) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, key + " requires a texture URI");
                continue;
            }
            auto& slot = key == "map_Kd" ? r.materials[activeMaterial].baseColor
                                         : r.materials[activeMaterial].emissive;
            AttachImage(r, lineNo, uri, &slot);
        } else if (key == "bump" || key == "map_bump" || key == "map_Bump" ||
                   key == "map_Bm") {
            if (activeMaterial < 0) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, "bump texture before any newmtl");
                continue;
            }
            std::string uri;
            if (!ExtractTextureUri(toks, &uri)) {
                AddDiag(r.diagnostics, ImportSeverity::Error, kCodeMtlSyntax,
                        lineNo, "bump map requires a texture URI");
                continue;
            }
            AttachImage(r, lineNo, uri, &r.materials[activeMaterial].normal);
        } else if (key == "map_Ks" || key == "map_Ns" || key == "map_refl" ||
                   key == "map_d" || key == "map_Tr" || key == "map_illum" ||
                   key == "Ns" || key == "Ni" || key == "illum" || key == "Tf" ||
                   key == "Ka") {
            if (!Contains(seenIgnored, key)) {
                seenIgnored.push_back(key);
                AddDiag(r.diagnostics, ImportSeverity::Warning,
                        kCodeMtlFieldIgnored, lineNo,
                        "MTL field has no v1 IR slot: " + key);
            }
        } else {
            if (!Contains(seenUnknown, key)) {
                seenUnknown.push_back(key);
                AddDiag(r.diagnostics, ImportSeverity::Warning,
                        kCodeMtlUnknownDirective, lineNo,
                        "unknown MTL directive: " + key);
            }
        }
        if (HasError(r.diagnostics)) break;
    }

    r.ok = !HasError(r.diagnostics);
    return r;
}

} // namespace Engine::AssetImport
