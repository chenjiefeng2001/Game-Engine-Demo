/**
 * @file SkinningComponent.cpp
 * @brief 蒙皮组件实现 — 蒙皮矩阵管线计算
 *
 * 蒙皮矩阵管线：
 *   1. SetSkeleton 时计算 inverseBindMatrix（绑定姿势的逆矩阵）
 *   2. AdvanceAnimation 时从动画时间线更新骨骼的局部姿势矩阵
 *   3. Skeleton::UpdateWorldPoses() 计算每个骨骼的 currentPoseMatrix（世界矩阵）
 *   4. 对每个骨骼：skinningMatrix = currentPoseMatrix * inverseBindMatrix
 *   5. 输出蒙皮矩阵数组供 GPU 使用
 */

#include "Engine/Animation/SkinningComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"

namespace Engine {

    // ============================================================
    // 生命周期
    // ============================================================

    void SkinningComponent::OnUpdate(float32 dt) {
        Component::OnUpdate(dt);

        // 自动推进动画（如果有的话）
        if (m_Animation && m_Animation->IsPlaying()) {
            AdvanceAnimation(dt);
        }
    }

    // ============================================================
    // 骨骼 — 设置时自动计算 inverseBindMatrix
    // ============================================================

    void SkinningComponent::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
        m_Skeleton = std::move(skeleton);
        m_PoseEvaluator = std::make_unique<AnimationPose>(m_Skeleton);

        // 初始化为绑定姿势
        if (m_Skeleton) {
            m_Skeleton->ResetToBindPose();
            m_SkinningMatrices = m_Skeleton->GetSkinningMatrices();
        }
    }

    // ============================================================
    // 蒙皮网格
    // ============================================================

    void SkinningComponent::SetSkinnedMesh(std::shared_ptr<SkinnedMesh> mesh) {
        m_SkinnedMesh = std::move(mesh);
    }

    // ============================================================
    // 动画
    // ============================================================

    void SkinningComponent::SetAnimation(std::shared_ptr<AnimationLocalTimeline> animation) {
        m_Animation = std::move(animation);
    }

    // ============================================================
    // 核心管线：推进动画 → 求值姿势 → 计算蒙皮矩阵
    // ============================================================

    void SkinningComponent::AdvanceAnimation(float32 dt) {
        if (!m_Animation || !m_Skeleton)
            return;

        // 1. 推进动画时间线（计算轨道值）
        m_Animation->AdvanceTime(dt);

        // 2. 求值当前姿势并更新蒙皮矩阵
        EvaluatePose();
    }

    void SkinningComponent::EvaluatePose() {
        if (!m_Skeleton || !m_PoseEvaluator)
            return;

        // 1. 从动画时间线求值骨骼的局部姿势矩阵（localPoseMatrix）
        //    轨道命名约定：轨道名称 = "BoneName.property"
        //    如 "Hips.position", "Spine.rotation", "Head.scale"
        if (m_Animation) {
            m_PoseEvaluator->EvaluateFromTimeline(*m_Animation);
        }

        // 2. 递归计算世界矩阵和蒙皮矩阵
        //    - 对每个骨骼：currentPoseMatrix = parentWorld * localPoseMatrix
        //    - 对每个骨骼：skinningMatrix = currentPoseMatrix * inverseBindMatrix
        m_Skeleton->UpdateWorldPoses();

        // 3. 同步蒙皮矩阵缓存
        m_SkinningMatrices = m_Skeleton->GetSkinningMatrices();
    }

    // ============================================================
    // 契约组件持久化
    // ============================================================
    //
    // 克隆保真要求：骨架、蒙皮网格、动画时间线与当前播放时间全部重建，
    // 使克隆出来的组件可以继续推进动画并产出有效蒙皮矩阵。
    // 这里序列化的是**结构与状态**，不共享任何运行时对象。

    namespace {

    void WriteMat4(nlohmann::json& j, const char* key, const Mat4& m) {
        j[key] = { m.Data()[0],  m.Data()[1],  m.Data()[2],  m.Data()[3],
                   m.Data()[4],  m.Data()[5],  m.Data()[6],  m.Data()[7],
                   m.Data()[8],  m.Data()[9],  m.Data()[10], m.Data()[11],
                   m.Data()[12], m.Data()[13], m.Data()[14], m.Data()[15] };
    }

    bool ReadMat4(const nlohmann::json& j, const char* key, Mat4& m) {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() != 16)
            return false;
        for (size_t i = 0; i < 16; ++i)
            m.Data()[i] = j[key][i].get<float>();
        return true;
    }

    } // namespace

    void SkinningComponent::Serialize(nlohmann::json& json) const {
        json["visible"] = m_Visible;

        if (m_Skeleton) {
            nlohmann::json bones = nlohmann::json::array();
            for (int32 i = 0; i < static_cast<int32>(m_Skeleton->GetBoneCount()); ++i) {
                const Bone& b = m_Skeleton->GetBone(i);
                nlohmann::json bj;
                bj["name"]  = b.name;
                bj["parent"] = b.parentIndex;
                WriteMat4(bj, "bind", b.bindMatrix);
                bones.push_back(std::move(bj));
            }
            json["skeleton"] = std::move(bones);
        }

        if (m_SkinnedMesh) {
            nlohmann::json mv;
            mv["boneMapping"] = m_SkinnedMesh->GetBoneMapping();

            nlohmann::json verts = nlohmann::json::array();
            for (const SkinnedVertex& v : m_SkinnedMesh->GetVertices()) {
                nlohmann::json vj;
                vj["pos"] = { v.position.x, v.position.y, v.position.z };
                vj["nrm"] = { v.normal.x,   v.normal.y,   v.normal.z   };
                vj["uv"]  = { v.texCoord.x, v.texCoord.y };
                vj["tan"] = { v.tangent.x,  v.tangent.y,  v.tangent.z  };
                vj["indices"] = { v.boneIndices[0], v.boneIndices[1],
                                  v.boneIndices[2], v.boneIndices[3] };
                vj["weights"] = { v.boneWeights[0], v.boneWeights[1],
                                  v.boneWeights[2], v.boneWeights[3] };
                verts.push_back(std::move(vj));
            }
            mv["vertices"] = std::move(verts);
            mv["indices"]  = m_SkinnedMesh->GetIndices();
            json["skinnedMesh"] = std::move(mv);
        }

        if (m_Animation) {
            nlohmann::json aj;
            aj["duration"]  = m_Animation->GetDuration();
            aj["localTime"] = m_Animation->GetLocalTime();
            aj["playing"]   = m_Animation->IsPlaying();
            aj["timeScale"] = m_Animation->GetTimeScale();

            // 命名轨道：只序列化实际存在的轨道，读回用 AddFloatTrack 还原
            // 同名条目（AddFloatTrack 对已存在轨道是幂等返回）。
            nlohmann::json tracks = nlohmann::json::array();
            for (int32 i = 0; i < static_cast<int32>(m_Animation->GetFloatTrackCount()); ++i) {
                const AnimationTrack* tr = m_Animation->GetFloatTrackAt(i);
                if (!tr) continue;
                nlohmann::json tj;
                tj["name"] = m_Animation->GetFloatTrackNameAt(i);
                tj["type"] = static_cast<int>(tr->GetPropertyType());
                nlohmann::json keys = nlohmann::json::array();
                for (const KeyFrameFloat& k : tr->GetFloatKeys()) {
                    keys.push_back({ k.time, k.value });
                }
                for (const KeyFrameVec3& k : tr->GetVec3Keys()) {
                    keys.push_back({ k.time, { k.value.x, k.value.y, k.value.z } });
                }
                tj["keys"] = std::move(keys);
                tracks.push_back(std::move(tj));
            }
            aj["tracks"] = std::move(tracks);
            json["animation"] = std::move(aj);
        }
    }

    bool SkinningComponent::Deserialize(const nlohmann::json& json) {
        if (!json.is_object()) return false;

        if (json.contains("visible") && json["visible"].is_boolean())
            m_Visible = json["visible"].get<bool>();

        // 骨架
        if (json.contains("skeleton") && json["skeleton"].is_array()) {
            auto sk = std::make_shared<Skeleton>();
            bool ok = true;
            for (const auto& bj : json["skeleton"]) {
                if (!bj.contains("name") || !bj.contains("bind")) { ok = false; break; }
                Mat4 bind;
                if (!ReadMat4(bj, "bind", bind)) { ok = false; break; }
                const std::string name = bj["name"].get<std::string>();
                const int32 parent = bj.value("parent", -1);
                if (parent < 0) {
                    sk->AddRootBone(name, bind);
                } else {
                    // 按索引引用父骨骼：这里用名称链重建，保持父子关系
                    const Bone& pb = sk->GetBone(parent);
                    sk->AddBone(name, pb.name, bind);
                }
            }
            if (ok) SetSkeleton(std::move(sk));
        }

        // 蒙皮网格
        if (json.contains("skinnedMesh") && json["skinnedMesh"].is_object()) {
            const auto& mj = json["skinnedMesh"];
            auto mesh = std::make_shared<SkinnedMesh>();
            if (mj.contains("boneMapping") && mj["boneMapping"].is_array())
                mesh->SetBoneMapping(mj["boneMapping"].get<std::vector<int32>>());

            std::vector<SkinnedVertex> verts;
            if (mj.contains("vertices") && mj["vertices"].is_array()) {
                for (const auto& vj : mj["vertices"]) {
                    SkinnedVertex v;
                    if (vj.contains("pos") && vj["pos"].size() >= 3)
                        v.position = Vec3(vj["pos"][0].get<float>(),
                                          vj["pos"][1].get<float>(),
                                          vj["pos"][2].get<float>());
                    if (vj.contains("nrm") && vj["nrm"].size() >= 3)
                        v.normal = Vec3(vj["nrm"][0].get<float>(),
                                        vj["nrm"][1].get<float>(),
                                        vj["nrm"][2].get<float>());
                    if (vj.contains("uv") && vj["uv"].size() >= 2)
                        v.texCoord = Vec2(vj["uv"][0].get<float>(),
                                          vj["uv"][1].get<float>());
                    if (vj.contains("tan") && vj["tan"].size() >= 3)
                        v.tangent = Vec3(vj["tan"][0].get<float>(),
                                         vj["tan"][1].get<float>(),
                                         vj["tan"][2].get<float>());
                    if (vj.contains("indices") && vj["indices"].size() >= 4) {
                        for (int k = 0; k < 4; ++k)
                            v.boneIndices[k] = vj["indices"][k].get<uint32>();
                    }
                    if (vj.contains("weights") && vj["weights"].size() >= 4) {
                        for (int k = 0; k < 4; ++k)
                            v.boneWeights[k] = vj["weights"][k].get<float>();
                    }
                    verts.push_back(v);
                }
            }
            if (mj.contains("indices") && mj["indices"].is_array())
                mesh->SetIndices(mj["indices"].get<std::vector<uint32>>());
            mesh->SetVertices(verts);
            SetSkinnedMesh(std::move(mesh));
        }

        // 动画时间线
        if (json.contains("animation") && json["animation"].is_object()) {
            const auto& aj = json["animation"];
            auto anim = std::make_shared<AnimationLocalTimeline>();
            if (aj.contains("duration"))  anim->SetDuration(aj["duration"].get<float>());
            if (aj.contains("timeScale")) anim->SetTimeScale(aj["timeScale"].get<float>());

            if (aj.contains("tracks") && aj["tracks"].is_array()) {
                for (const auto& tj : aj["tracks"]) {
                    if (!tj.contains("name") || !tj.contains("keys")) continue;
                const std::string tname = tj["name"].get<std::string>();
                AnimationTrack& tr = anim->AddFloatTrack(tname);
                tr.ClearKeyFrames();
                    for (const auto& kj : tj["keys"]) {
                        if (!kj.is_array() || kj.empty()) continue;
                        const float time = kj[0].get<float>();
                        // 值可能是标量（float）或数组（Vec2/3/4），
                        // 按实际形状判断，而不是靠外层长度。
                        if (kj.size() >= 2 && kj[1].is_array()) {
                            const auto& v = kj[1];
                            if (v.size() >= 4) {
                                tr.AddKeyFrame(KeyFrameVec4{
                                    time,
                                    Vec4(v[0].get<float>(), v[1].get<float>(),
                                         v[2].get<float>(), v[3].get<float>()) });
                            } else if (v.size() >= 3) {
                                tr.AddKeyFrame(KeyFrameVec3{
                                    time,
                                    Vec3(v[0].get<float>(), v[1].get<float>(),
                                         v[2].get<float>()) });
                            } else if (v.size() >= 2) {
                                tr.AddKeyFrame(KeyFrameVec2{
                                    time,
                                    Vec2(v[0].get<float>(), v[1].get<float>()) });
                            }
                        } else if (kj.size() >= 2 && kj[1].is_number()) {
                            tr.AddKeyFrame(KeyFrameFloat{ time, kj[1].get<float>() });
                        }
                    }
                }
            }
            SetAnimation(std::move(anim));

            // 还原播放位置与状态，使克隆体从同一时间点继续
            if (m_Animation) {
                if (aj.contains("localTime"))
                    m_Animation->Seek(aj["localTime"].get<float>());
                if (aj.value("playing", false)) m_Animation->Play();
            }
        }

        // 让克隆体立即拥有有效姿势与蒙皮矩阵
        EvaluatePose();
        return true;
    }

    void SkinningComponent::Register() {
        ComponentRegistryGo::Register("Skinning", []() -> std::shared_ptr<Component> {
            return std::make_shared<SkinningComponent>();
        });
    }

} // namespace Engine
