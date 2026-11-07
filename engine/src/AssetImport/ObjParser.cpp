#include "ObjParser.h"

#include "AssetFileReader.h"
#include "ImportLimits.h"
#include "MtlParser.h"

#include <charconv>

#include <map>
#include <tuple>

namespace Engine::AssetImport {

namespace {

constexpr uint32_t kMaterialNoneSentinel = 0xFFFFFFFFu;

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

bool ParseIndex(const std::string& token, int64_t* out) {
    if (token.empty()) return false;
    const auto r =
        std::from_chars(token.data(), token.data() + token.size(), *out);
    if (r.ec != std::errc() || r.ptr != token.data() + token.size()) return false;
    return true;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

// One OBJ face corner: "i", "i/j", "i//k", "i/j/k" (1-based; negative ok).
struct FaceCorner
{
    int64_t v = 0;
    bool hasVt = false;
    int64_t vt = -1;
    bool hasVn = false;
    int64_t vn = -1;
};

// Parses "i", "i/j", "i//k", "i/j/k". Dangling or empty slash segments are
// OBJ syntax errors (fail-closed); substring guards prevent OOB slicing.
bool ParseFaceCorner(const std::string& token, FaceCorner* c) {
    const size_t slash = token.find('/');
    if (slash == std::string::npos) return ParseIndex(token, &c->v);
    if (slash == 0) return false;
    if (slash + 1 < token.size() && token[slash + 1] == '/') {
        if (slash + 2 >= token.size()) return false; // "1//" -> empty vn
        std::string vTok = token.substr(0, slash);
        std::string vnTok = token.substr(slash + 2);
        if (vnTok.empty()) return false;
        c->hasVn = true;
        if (!ParseIndex(vTok, &c->v)) return false;
        if (!ParseIndex(vnTok, &c->vn)) return false;
        return true;
    }
    std::string vTok = token.substr(0, slash);
    const size_t slash2 = token.find('/', slash + 1);
    if (slash2 == std::string::npos) {
        if (slash + 1 >= token.size()) return false; // "1/" -> empty vt
        std::string vtTok = token.substr(slash + 1);
        if (vtTok.empty()) return false;
        c->hasVt = true;
        if (!ParseIndex(vTok, &c->v)) return false;
        if (!ParseIndex(vtTok, &c->vt)) return false;
        return true;
    }
    std::string vtTok = token.substr(slash + 1, slash2 - slash - 1);
    if (slash2 + 1 > token.size()) return false;
    std::string vnTok = token.substr(slash2 + 1);
    if (vtTok.empty() || vnTok.empty()) return false;
    c->hasVt = true;
    c->hasVn = true;
    if (!ParseIndex(vTok, &c->v)) return false;
    if (!ParseIndex(vtTok, &c->vt)) return false;
    if (!ParseIndex(vnTok, &c->vn)) return false;
    return true;
}

// Face/segment identity: (object, group, material) as seen at the face line.
struct FaceKey
{
    std::string object;
    std::string group;
    std::string material;

    bool operator==(const FaceKey& o) const {
        return object == o.object && group == o.group &&
               material == o.material;
    }
};

struct ObjState
{
    MeshImportIR* out = nullptr;
    std::vector<ImportDiagnostic>* diag = nullptr;

    std::vector<float> vPos, vtUv, vnNrm;
    uint32_t vCount = 0, vtCount = 0, vnCount = 0;

    std::vector<MaterialIR> mtlMaterials; // merged from mtllibs
    std::vector<std::string> missingMats; // first-use referenced names
    std::vector<std::string> dupMaterialNames; // dedupe warnings

    bool inSpan = false;
    MeshPrimitiveIR pendingSpan;
    FaceKey prevFaceKey;
    bool havePrevFaceKey = false;

    std::string idObj, idGroup, idMaterial;

    uint32_t totalInstancedVertices = 0;
    bool smoothWarned = false;
    bool unspecifiedWarned = false;
    std::vector<std::string> unknownSeen;
    std::vector<std::string> linePointSeen;

    uint32_t MaterialSlotFor(const FaceKey& key, uint32_t line) {
        if (key.material.empty()) {
            if (!unspecifiedWarned) {
                unspecifiedWarned = true;
                AddDiag(*diag, ImportSeverity::Warning,
                        kCodeMaterialUnspecified, line,
                        "OBJ faces without usemtl map to no material slot");
            }
            return kMaterialNoneSentinel;
        }
        for (uint32_t i = 0; i < mtlMaterials.size(); ++i) {
            if (mtlMaterials[i].name == key.material) return i;
        }
        for (uint32_t i = 0; i < missingMats.size(); ++i) {
            if (missingMats[i] == key.material) {
                return static_cast<uint32_t>(mtlMaterials.size()) + i;
            }
        }
        AddDiag(*diag, ImportSeverity::Warning, kCodeMtlMaterialMissing, line,
                "usemtl references undefined material; slot synthesized: " +
                    key.material);
        missingMats.push_back(key.material);
        return static_cast<uint32_t>(mtlMaterials.size()) +
               static_cast<uint32_t>(missingMats.size()) - 1;
    }

    void OpenSpan(const FaceKey& key, uint32_t line) {
        pendingSpan = MeshPrimitiveIR();
        pendingSpan.firstIndex = static_cast<uint32_t>(out->indices.size());
        pendingSpan.materialIndex = MaterialSlotFor(key, line);
        pendingSpan.nodeName = "o:" + key.object + "/g:" + key.group;
        inSpan = true;
        prevFaceKey = key;
        havePrevFaceKey = true;
    }

    void CloseSpan() {
        if (!inSpan) return;
        pendingSpan.indexCount = static_cast<uint32_t>(out->indices.size()) -
                                 pendingSpan.firstIndex;
        out->primitives.push_back(pendingSpan);
        inSpan = false;
    }

    bool ResolveOneBased(int64_t idx, uint32_t count, uint32_t* abs,
                         uint32_t line, const char* what) {
        int64_t resolved = idx;
        if (idx < 0) resolved = static_cast<int64_t>(count) + idx + 1;
        if (resolved <= 0 || resolved > static_cast<int64_t>(count)) {
            AddDiag(*diag, ImportSeverity::Error, kCodeObjIndexOutOfRange,
                    line,
                    std::string(what) + " index out of range: " +
                        std::to_string(idx) + " (stream size " +
                        std::to_string(count) + ")");
            return false;
        }
        *abs = static_cast<uint32_t>(resolved);
        return true;
    }

    bool GetOrCreateVertex(const FaceCorner& c, uint32_t line, uint32_t* outIdx) {
        uint32_t posIdx = 0;
        if (!ResolveOneBased(c.v, vCount, &posIdx, line, "v")) return false;
        bool hasUv = false, hasNrm = false;
        uint32_t uvAbs = 0, nrmAbs = 0;
        if (c.hasVt) {
            if (!ResolveOneBased(c.vt, vtCount, &uvAbs, line, "vt")) return false;
            hasUv = true;
        }
        if (c.hasVn) {
            if (!ResolveOneBased(c.vn, vnCount, &nrmAbs, line, "vn")) return false;
            hasNrm = true;
        }
        const auto dedupeKey = std::make_tuple(
            static_cast<int64_t>(posIdx),
            c.hasVt ? static_cast<int64_t>(uvAbs) : -1,
            c.hasVn ? static_cast<int64_t>(nrmAbs) : -1);
        auto it = dedupe.find(dedupeKey);
        if (it != dedupe.end()) {
            *outIdx = it->second;
            return true;
        }
        if (totalInstancedVertices + 1 > kMaxTotalVertices) {
            AddDiag(*diag, ImportSeverity::Error, kCodeLimitVertices,
                    line, "total vertex count exceeds kMaxTotalVertices");
            return false;
        }
        totalInstancedVertices++;
        ImportVertex v;
        const size_t pv = static_cast<size_t>(posIdx - 1) * 3;
        v.position[0] = vPos[pv];
        v.position[1] = vPos[pv + 1];
        v.position[2] = vPos[pv + 2];
        if (hasNrm) {
            const size_t pn = static_cast<size_t>(nrmAbs - 1) * 3;
            v.normal[0] = vnNrm[pn];
            v.normal[1] = vnNrm[pn + 1];
            v.normal[2] = vnNrm[pn + 2];
            v.flags |= kVertexFlagNormalValid;
        }
        if (hasUv) {
            const size_t pu = static_cast<size_t>(uvAbs - 1) * 2;
            v.uv0[0] = vtUv[pu];
            v.uv0[1] = 1.0f - vtUv[pu + 1]; // single conversion point (B1 protocol)
            v.flags |= kVertexFlagUvValid;
        }
        out->vertices.push_back(v);
        *outIdx = static_cast<uint32_t>(out->vertices.size()) - 1;
        dedupe.emplace(dedupeKey, *outIdx);
        return true;
    }

    bool EmitFace(const std::vector<FaceCorner>& corners, uint32_t line) {
        if (corners.size() < 3) {
            AddDiag(*diag, ImportSeverity::Error, kCodeObjFaceDegenerate,
                    line, "OBJ face must contain at least 3 corners");
            return false;
        }
        if (corners.size() > 3) {
            AddDiag(*diag, ImportSeverity::Warning,
                    kCodeObjPolygonTriangulated, line,
                    "polygon triangulated by fan rule");
        }
        FaceKey key{idObj, idGroup, idMaterial};
        if (!inSpan || !(key == prevFaceKey) || !havePrevFaceKey) {
            if (inSpan) CloseSpan();
            if (out->primitives.size() >= kMaxPrimitives) {
                AddDiag(*diag, ImportSeverity::Error, kCodeLimitPrimitives,
                        line, "primitive span count exceeds kMaxPrimitives");
                return false;
            }
            OpenSpan(key, line);
        } else {
            prevFaceKey = key;
        }
        havePrevFaceKey = true;

        std::vector<uint32_t> cornerIds;
        cornerIds.reserve(corners.size());
        for (const FaceCorner& c : corners) {
            uint32_t vid = 0;
            if (!GetOrCreateVertex(c, line, &vid)) return false;
            cornerIds.push_back(vid);
        }
        for (size_t k = 2; k < cornerIds.size(); ++k) {
            out->indices.push_back(cornerIds[0]);
            out->indices.push_back(cornerIds[k - 1]);
            out->indices.push_back(cornerIds[k]);
        }
        return true;
    }

    std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> dedupe;
};

void Tokenize(const std::string& raw, std::vector<std::string>* toks) {
    toks->clear();
    for (size_t i = 0; i < raw.size();) {
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
        if (i >= raw.size()) break;
        const size_t j = i;
        while (i < raw.size() && raw[i] != ' ' && raw[i] != '\t') ++i;
        toks->push_back(raw.substr(j, i - j));
    }
}

} // namespace

ImportResult ObjParser::ParseObj(std::vector<uint8_t> objBytes,
                                 const std::string& sourceDir) {
    ImportResult result;
    if (objBytes.size() > kMaxSourceFileBytes) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeLimitSourceSize;
        d.location = "file";
        d.message = "source exceeds kMaxSourceFileBytes";
        result.diagnostics.push_back(std::move(d));
        return result;
    }
    const std::string text(reinterpret_cast<const char*>(objBytes.data()),
                           objBytes.size());

    ObjState s;
    s.out = &result.mesh;
    s.diag = &result.diagnostics;

    std::vector<std::string> mtllibSeen;
    uint32_t imageBase = 0;
    (void)imageBase;

    std::vector<std::string> lines;
    {
        size_t cursor = 0;
        while (cursor <= text.size()) {
            const size_t eol = text.find('\n', cursor);
            if (eol == std::string::npos) {
                lines.push_back(text.substr(cursor));
                cursor = text.size() + 1;
                break;
            }
            lines.push_back(text.substr(cursor, eol - cursor));
            cursor = eol + 1;
        }
    }

    // ── pass 1: v/vt/vn streams + mtllib preloading (directives silent) ──
    for (uint32_t lineNo = 1; lineNo <= lines.size(); ++lineNo) {
        std::string raw = lines[lineNo - 1];
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::vector<std::string> toks;
        Tokenize(raw, &toks);
        if (toks.empty() || toks[0].rfind('#', 0) == 0) continue;
        const std::string& key = toks[0];

        if (key == "v" || key == "vt" || key == "vn") {
            std::vector<float> vals;
            bool numeric = true;
            const size_t needed = key == "vt" ? 3 : 4;
            for (size_t i = 1; i < toks.size() && i < needed; ++i) {
                float f = 0.f;
                if (!ParseFloat(toks[i].c_str(), toks[i].c_str() + toks[i].size(), &f)) {
                    AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax,
                            lineNo, key + " requires numeric components");
                    numeric = false;
                    break;
                }
                vals.push_back(f);
            }
            const size_t minimum = key == "vt" ? 2 : 3;
            if (numeric && vals.size() >= minimum) {
                if (key == "v") {
                    s.vPos.push_back(vals[0]);
                    s.vPos.push_back(vals[1]);
                    s.vPos.push_back(vals[2]);
                    if (++s.vCount > kMaxTotalVertices) {
                        AddDiag(*s.diag, ImportSeverity::Error,
                                kCodeLimitVertices, lineNo,
                                "v count exceeds kMaxTotalVertices");
                        break;
                    }
                } else if (key == "vt") {
                    s.vtUv.push_back(vals[0]);
                    s.vtUv.push_back(vals[1]);
                    if (++s.vtCount > kMaxTotalVertices) {
                        AddDiag(*s.diag, ImportSeverity::Error,
                                kCodeLimitVertices, lineNo,
                                "vt count exceeds kMaxTotalVertices");
                        break;
                    }
                } else {
                    s.vnNrm.push_back(vals[0]);
                    s.vnNrm.push_back(vals[1]);
                    s.vnNrm.push_back(vals[2]);
                    if (++s.vnCount > kMaxTotalVertices) {
                        AddDiag(*s.diag, ImportSeverity::Error,
                                kCodeLimitVertices, lineNo,
                                "vn count exceeds kMaxTotalVertices");
                        break;
                    }
                }
                continue;
            }
            if (!numeric) break;
            AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                    key + " requires at least " +
                        std::to_string(minimum) + " components");
            break;
        }
        if (key == "mtllib") {
            if (toks.size() < 2) {
                AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                        "mtllib requires a library name");
                break;
            }
            bool hardFail = false;
            for (size_t t = 1; t < toks.size() && !hardFail; ++t) {
                const std::string& uri = toks[t];
                const size_t diagStart = s.diag->size();
                ReportUriFailure(*s.diag, uri,
                                 "mtllib line " + std::to_string(lineNo));
                for (size_t k = diagStart; k < s.diag->size(); ++k) {
                    if ((*s.diag)[k].severity == ImportSeverity::Error) {
                        hardFail = true;
                        break;
                    }
                }
                if (hardFail) break;
                if (Contains(mtllibSeen, uri)) continue;
                mtllibSeen.push_back(uri);
                std::vector<uint8_t> mtlBytes;
                if (!ReadAssetFile(sourceDir, uri, &mtlBytes, s.diag)) {
                    // downgrade read failures to warning; URI errors fatal
                    bool fellBack = false;
                    for (size_t k = diagStart; k < s.diag->size(); ++k) {
                        if ((*s.diag)[k].code == std::string(kCodeExternalBufferReadFail)) {
                            (*s.diag)[k].severity = ImportSeverity::Warning;
                            (*s.diag)[k].code = kCodeMtlLibMissing;
                            (*s.diag)[k].location = "mtllib line " + std::to_string(lineNo);
                            fellBack = true;
                        }
                    }
                    (void)fellBack;
                    if (HasError(*s.diag)) hardFail = true;
                    continue;
                }
                MtlParseResult mtl = MtlParser::ParseMtl(std::move(mtlBytes));
                for (const auto& d : mtl.diagnostics) {
                    ImportDiagnostic x = d;
                    x.location += ", in " + uri;
                    s.diag->push_back(std::move(x));
                }
                if (!mtl.ok) {
                    hardFail = true;
                    break;
                }
                if (!Contains(s.out->dependencyFiles, uri)) {
                    s.out->dependencyFiles.push_back(uri);
                }
                for (MaterialIR& m : mtl.materials) {
                    if (m.baseColor.present) m.baseColor.imageIndex += imageBase;
                    if (m.normal.present) m.normal.imageIndex += imageBase;
                    if (m.emissive.present) m.emissive.imageIndex += imageBase;
                }
                for (const TextureImageIR& img : mtl.images) {
                    TextureImageIR copy = img;
                    copy.sourceImageIndex = static_cast<uint32_t>(s.out->images.size());
                    s.out->images.push_back(std::move(copy));
                }
                imageBase += static_cast<uint32_t>(mtl.images.size());
                for (const MaterialIR& m : mtl.materials) {
                    if (s.mtlMaterials.size() >= kMaxMaterials) {
                        AddDiag(*s.diag, ImportSeverity::Error,
                                kCodeLimitMaterials, lineNo,
                                "material count exceeds kMaxMaterials");
                        hardFail = true;
                        break;
                    }
                    if (Contains(s.dupMaterialNames, m.name)) continue;
                    bool duplicate = false;
                    for (const MaterialIR& existing : s.mtlMaterials) {
                        if (existing.name == m.name && !m.name.empty()) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        if (!Contains(s.dupMaterialNames, m.name)) {
                            s.dupMaterialNames.push_back(m.name);
                            AddDiag(*s.diag, ImportSeverity::Warning,
                                    kCodeMtlDuplicateMaterial, lineNo,
                                    "duplicate material name keeps first definition: " +
                                        m.name);
                        }
                        continue;
                    }
                    s.mtlMaterials.push_back(m);
                }
            }
            if (hardFail) break;
            continue;
        }
        if (key == "usemtl" || key == "o" || key == "g" || key == "s" ||
            key == "f" || key == "l" || key == "p") {
            // deferred to pass 2 (o/g/usemtl segmentation, face loop)
            if (key == "s" && !s.smoothWarned) {
                s.smoothWarned = true;
                AddDiag(*s.diag, ImportSeverity::Warning, kCodeObjSmoothIgnored,
                        lineNo, "smoothing groups ignored in v1 IR");
            }
            if (key == "l" || key == "p") {
                if (!Contains(s.linePointSeen, key)) {
                    s.linePointSeen.push_back(key);
                    AddDiag(*s.diag, ImportSeverity::Warning,
                            kCodeObjUnknownDirective, lineNo,
                            "OBJ line/point primitives ignored in v1: " + key);
                }
            }
            continue;
        }
        if (!Contains(s.unknownSeen, key)) {
            s.unknownSeen.push_back(key);
            AddDiag(*s.diag, ImportSeverity::Warning, kCodeObjUnknownDirective,
                    lineNo, "unknown OBJ directive: " + key);
        }
    }
    if (HasError(*s.diag)) return result;

    // ── pass 2: faces + segmentation + material resolution ───────────────
    for (uint32_t lineNo = 1; lineNo <= lines.size(); ++lineNo) {
        std::string raw = lines[lineNo - 1];
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::vector<std::string> toks;
        Tokenize(raw, &toks);
        if (toks.empty() || toks[0].rfind('#', 0) == 0) continue;
        const std::string& key = toks[0];

        if (key == "o") {
            if (toks.size() < 2) {
                AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                        "o requires a name");
                break;
            }
            s.idObj = toks[1];
            continue;
        }
        if (key == "g") {
            if (toks.size() < 2) {
                AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                        "g requires a name");
                break;
            }
            s.idGroup = toks[1];
            continue;
        }
        if (key == "usemtl") {
            if (toks.size() < 2) {
                AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                        "usemtl requires a material name");
                break;
            }
            s.idMaterial = toks[1];
            continue;
        }
        if (key != "f") continue;

        std::vector<FaceCorner> corners;
        bool ok = true;
        for (size_t t = 1; t < toks.size(); ++t) {
            FaceCorner c;
            if (!ParseFaceCorner(toks[t], &c)) {
                AddDiag(*s.diag, ImportSeverity::Error, kCodeObjSyntax, lineNo,
                        "malformed face corner: " + toks[t]);
                ok = false;
                break;
            }
            corners.push_back(c);
        }
        if (!ok) break;
        if (!s.EmitFace(corners, lineNo)) break;
        if (HasError(*s.diag)) break;
    }
    if (HasError(*s.diag)) return result;

    if (s.out->indices.empty()) {
        AddDiag(*s.diag, ImportSeverity::Error, kCodeObjFacesMissing, 0,
                "OBJ contains no geometry faces");
        return result;
    }
    s.CloseSpan(); // finalize trailing span

    // finalize materials: mtllib set first, then synthetic fallback slots
    for (const std::string& missing : s.missingMats) {
        MaterialIR m;
        m.name = missing;
        s.mtlMaterials.push_back(std::move(m));
    }
    result.mesh.materials = std::move(s.mtlMaterials);

    result.ok = !HasError(result.diagnostics);
    return result;
}

} // namespace Engine::AssetImport


