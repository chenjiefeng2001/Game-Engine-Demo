/**
 * @file M1FCrashHarness.cpp
 * @brief M1-F：在指定持久化边界终止自身的子进程测试工具
 *
 * 用法：
 *   m1f_crash_harness save <projectRoot> <point>
 *   m1f_crash_harness first-save <projectRoot> <point>
 *   m1f_crash_harness rename <projectRoot> <point>
 *
 * 正常完成退出 0；故障点触发时 _Exit(90)；前置失败退出 2。
 */

#include "editor_bridge/capi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr const char* kManifest = "assets/gp01/manifest.json";
constexpr const char* kScene = "assets/gp01/Main.scene";

int ParseRevisionPoint(const char* name) {
    // 与 Engine::Content::RevisionFaultPoint 顺序一致。
    const char* names[] = {
        "BeforeCandidateManifestWrite",
        "AfterManifestWrite",
        "AfterSceneWrite",
        "AfterCandidateValidation",
        "BeforeMarkerWrite",
        "AfterMarkerWrite",
        "BeforeCurrentReplace",
        "AfterCurrentReplaceBeforeReadback",
    };
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, names[i]) == 0) return i;
    return -1;
}

struct CrashContext {
    int point = -1;
};

void RevisionCrashHook(int32_t point, void* userData) {
    auto* ctx = static_cast<CrashContext*>(userData);
    if (point == ctx->point) std::_Exit(90);
}

void ContentCrashHook(int32_t point, void* userData) {
    auto* ctx = static_cast<CrashContext*>(userData);
    // ContentFaultPoint::BeforeRenameSourceCleanup == 0
    if (point == ctx->point) std::_Exit(90);
}

int32_t FindAsset(EditorSessionHandle session, const char* path) {
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i) {
        char buf[512] = {};
        if (EditorSession_GetAssetPath(session, i, buf, sizeof(buf)) >= 0 &&
            std::strcmp(buf, path) == 0) {
            return i;
        }
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::string mode = argv[1];
    const std::string root = argv[2];
    const std::string pointName = argv[3];

    CrashContext ctx;
    if (mode == "rename") {
        if (pointName != "BeforeRenameSourceCleanup") return 2;
        ctx.point = 0;
        EditorSession_SetContentFaultHookForTesting(&ContentCrashHook, &ctx);
    } else if (mode == "save" || mode == "first-save") {
        ctx.point = ParseRevisionPoint(pointName.c_str());
        if (ctx.point < 0) return 2;
        EditorSession_SetRevisionFaultHookForTesting(&RevisionCrashHook, &ctx);
    } else {
        return 2;
    }

    if (EditorBridge_RegisterMainThread() != 1) return 2;
    EditorSessionHandle session = EditorSession_Create();
    if (!session) return 2;
    if (EditorSession_OpenProjectRooted(session, root.c_str(), kManifest,
                                        kScene) != 1) {
        EditorSession_Destroy(session);
        return 2;
    }

    if (mode == "rename") {
        const int32_t index =
            FindAsset(session, "assets/gp01/tex/pad.png");
        if (index < 0 ||
            EditorSession_RenameAsset(session, index, "pad_crash") != 0) {
            EditorSession_Destroy(session);
            return 2;
        }
    } else {
        if (EditorSession_CreateEntity(session, "CrashProbe") < 0) {
            EditorSession_Destroy(session);
            return 2;
        }
        if (EditorSession_SaveProject(session) != 1) {
            EditorSession_Destroy(session);
            return 2;
        }
    }

    EditorSession_Destroy(session);
    return 0;
}
