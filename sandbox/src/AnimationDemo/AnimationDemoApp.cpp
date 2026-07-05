/**
 * @file AnimationDemoApp.cpp
 * @brief 3D 动画演示场景实现
 */

#include "AnimationDemoApp.h"
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/UIManager.h>
#include <Engine/MemoryTracker.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Log.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
#include <chrono>

namespace Engine {

constexpr int WW = 1280, WH = 720;

AnimationDemoApp::AnimationDemoApp(IGraphicsFactory& f)
    : m_Factory(f), m_TextureManager(f), m_SceneRenderer(f, m_TextureManager)
    , m_Camera(45, (float)WW/WH, 0.1f, 100), m_Scene("AnimDemo")
{
    m_Window = m_Factory.CreateWindow(WW, WH, "3D Animation Demo");
    if (!m_Window) {
        std::cerr << "[AnimDemo] Failed to create window!\n";
        return;
    }
    m_InputManager.Init(m_Window.get());
    m_SceneRenderer.SetRenderContext(*m_Window->GetContext());
    m_3DShader = m_Factory.CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
    if (!m_3DShader) {
        std::cerr << "[AnimDemo] Failed to load 3D shader!\n";
        return;
    }
    m_SceneRenderer.SetShader(m_3DShader);
    auto* ctx = m_Window->GetContext();
    if (!ctx) {
        std::cerr << "[AnimDemo] No render context!\n";
        return;
    }
    m_MeshRenderer = std::make_shared<MeshRenderer>(m_Factory, *ctx);
    m_MeshRenderer->SetShader(m_3DShader);
    m_MeshRenderer->SetCamera(&m_Camera);
    m_CubeMesh = std::make_shared<Mesh>(Mesh::CreateCube(1));
    m_SphereMesh = std::make_shared<Mesh>(Mesh::CreateSphere(0.5f, 16));
    m_CylinderMesh = std::make_shared<Mesh>(Mesh::CreateCylinder(0.5f, 1, 16));
    InitUI(); InitScene();
    std::cout << "[AnimDemo] OK\n";
}

AnimationDemoApp::~AnimationDemoApp() {
    if (m_UIInitialized) { UIManager::Shutdown(); m_UIInitialized = false; }
}

bool AnimationDemoApp::InitUI() {
    auto ui = m_Factory.CreateUIManager();
    if (!ui) return false;
    m_UIInitialized = UIManager::Init(std::move(ui), m_Window->GetNativeHandle(), m_Window->GetContext());
    return m_UIInitialized;
}

// ════════════════════════════════════════════
// 骨骼
// ════════════════════════════════════════════

static Mat4 MakeTrans(float x, float y, float z) {
    Mat4 m; m.Identity(); m.data[12]=x; m.data[13]=y; m.data[14]=z; return m;
}

std::shared_ptr<Skeleton> AnimationDemoApp::MakeHumanoid() {
    auto sk = std::make_shared<Skeleton>();
    sk->AddRootBone("Hips", MakeTrans(0,0,0));
    sk->AddBone("Spine","Hips", MakeTrans(0,1.2f,0));
    sk->AddBone("Head","Spine", MakeTrans(0,0.8f,0));
    sk->AddBone("LArm","Spine", MakeTrans(-0.6f,0.5f,0));
    sk->AddBone("LFore","LArm", MakeTrans(0,-0.7f,0));
    sk->AddBone("RArm","Spine", MakeTrans(0.6f,0.5f,0));
    sk->AddBone("RFore","RArm", MakeTrans(0,-0.7f,0));
    for (int i=0;i<(int)sk->GetBoneCount();++i) {
        Bone& b=sk->GetBone(i);
        Vec3 p=AnimationBlend::DecomposeTranslation(b.bindMatrix);
        Quat r=AnimationBlend::FromMatrix(b.bindMatrix);
        Vec3 s=AnimationBlend::DecomposeScale(b.bindMatrix);
        AnimationPose::ComposeMatrix(p,AnimationBlend::ToEuler(r),s,b.localPoseMatrix);
    }
    sk->UpdateWorldPoses(); return sk;
}

std::shared_ptr<AnimationResource> AnimationDemoApp::MakeRes(std::shared_ptr<Skeleton> sk) {
    auto r = std::make_shared<AnimationResource>(); r->skeleton = sk;
    auto id = std::make_shared<AnimationLocalTimeline>("Idle");
    id->SetDuration(1); id->SetLoopMode(AnimationLoopMode::Loop);
    auto& st=id->AddFloatTrack("Spine.position.y");
    st.AddKeyFrame(KeyFrameFloat{0,0}); st.AddKeyFrame(KeyFrameFloat{0.5f,0.05f}); st.AddKeyFrame(KeyFrameFloat{1,0});
    r->AddClip("Idle",id);
    auto wk=std::make_shared<AnimationLocalTimeline>("Walk");
    wk->SetDuration(0.6f); wk->SetLoopMode(AnimationLoopMode::Loop);
    auto& ws=wk->AddFloatTrack("Spine.position.y");
    ws.AddKeyFrame(KeyFrameFloat{0,0}); ws.AddKeyFrame(KeyFrameFloat{0.3f,0.08f}); ws.AddKeyFrame(KeyFrameFloat{0.6f,0});
    auto& la=wk->AddFloatTrack("LArm.rotation.x");
    la.AddKeyFrame(KeyFrameFloat{0,0}); la.AddKeyFrame(KeyFrameFloat{0.3f,30}); la.AddKeyFrame(KeyFrameFloat{0.6f,0});
    r->AddClip("Walk",wk); return r;
}

static void AddDisp(AnimatedCharacter& c, const std::string& bn, const Vec3& cl, const Vec3& off, const Vec3& scl, std::shared_ptr<Mesh> m) {
    int idx=c.skeleton->FindBoneIndex(bn); if(idx>=0) c.displays.push_back({idx,m,cl,off,scl});
}

AnimatedCharacter* AnimationDemoApp::MakeWalker() {
    auto c=std::make_unique<AnimatedCharacter>(); c->name="Walker";
    c->skeleton=MakeHumanoid(); c->resource=MakeRes(c->skeleton);
    c->controller=std::make_unique<AnimationController>(c->skeleton,c->resource);
    c->controller->AddLayer("Base",1); c->controller->AddState("Base","Idle",nullptr);
    c->controller->AddState("Base","Walk",nullptr); c->controller->SetInitialState("Base","Walk");
    c->controller->AddTransition("Base","Idle","Walk",TransitionType::CrossFade,0.3f,EaseCurveType::SmoothStep,
        [](const ParamSystem& p){return p.GetFloat("Speed")>0.1f;});
    c->controller->AddTransition("Base","Walk","Idle",TransitionType::CrossFade,0.3f,EaseCurveType::SmoothStep,
        [](const ParamSystem& p){return p.GetFloat("Speed")<0.1f;});
    c->constraintSolver=std::make_unique<ConstraintSolver>();
    int rf=c->skeleton->FindBoneIndex("RFore"); if(rf>=0) c->constraintSolver->AddBoneLocator("walker_hand",rf,Vec3(0,-0.4f,0));
    AddDisp(*c,"Hips",Vec3(0.2f,0.6f,1),Vec3(),Vec3(0.8f,0.6f,0.5f),m_CubeMesh);
    AddDisp(*c,"Spine",Vec3(0.3f,0.7f,1),Vec3(),Vec3(0.7f,0.7f,0.4f),m_CubeMesh);
    AddDisp(*c,"Head",Vec3(1,0.9f,0.6f),Vec3(0,0.1f,0),Vec3(0.5f,0.4f,0.5f),m_SphereMesh);
    AddDisp(*c,"LArm",Vec3(0.4f,0.8f,1),Vec3(),Vec3(0.3f,0.5f,0.3f),m_CylinderMesh);
    AddDisp(*c,"LFore",Vec3(0.5f,0.8f,1),Vec3(),Vec3(0.25f,0.5f,0.25f),m_CylinderMesh);
    AddDisp(*c,"RArm",Vec3(0.4f,0.8f,1),Vec3(),Vec3(0.3f,0.5f,0.3f),m_CylinderMesh);
    AddDisp(*c,"RFore",Vec3(0.5f,0.8f,1),Vec3(),Vec3(0.25f,0.5f,0.25f),m_CylinderMesh);
    auto* p=c.get(); m_Characters.push_back(std::move(c)); return p;
}

AnimatedCharacter* AnimationDemoApp::MakeBouncer() {
    auto c=std::make_unique<AnimatedCharacter>(); c->name="Bouncer";
    auto sk=std::make_shared<Skeleton>();
    auto m4=[](float y){Mat4 m;m.Identity();m.data[13]=y;return m;};
    sk->AddRootBone("Body",m4(0)); sk->AddBone("Head","Body",m4(1));
    for(int i=0;i<(int)sk->GetBoneCount();++i){
        Bone& b=sk->GetBone(i); Vec3 p=AnimationBlend::DecomposeTranslation(b.bindMatrix);
        Quat r=AnimationBlend::FromMatrix(b.bindMatrix); Vec3 s=AnimationBlend::DecomposeScale(b.bindMatrix);
        AnimationPose::ComposeMatrix(p,AnimationBlend::ToEuler(r),s,b.localPoseMatrix);
    }
    sk->UpdateWorldPoses(); c->skeleton=sk; c->resource=MakeRes(sk);
    auto bnc=std::make_shared<AnimationLocalTimeline>("Bounce");
    bnc->SetDuration(0.8f); bnc->SetLoopMode(AnimationLoopMode::Loop);
    auto& by=bnc->AddFloatTrack("Body.position.y");
    by.AddKeyFrame(KeyFrameFloat{0,0}); by.AddKeyFrame(KeyFrameFloat{0.2f,-0.3f});
    by.AddKeyFrame(KeyFrameFloat{0.4f,0.8f}); by.AddKeyFrame(KeyFrameFloat{0.6f,0});
    by.AddKeyFrame(KeyFrameFloat{0.8f,0});
    auto& bsy=bnc->AddFloatTrack("Body.scale.y");
    bsy.AddKeyFrame(KeyFrameFloat{0,1}); bsy.AddKeyFrame(KeyFrameFloat{0.2f,0.6f});
    bsy.AddKeyFrame(KeyFrameFloat{0.4f,1.4f}); bsy.AddKeyFrame(KeyFrameFloat{0.6f,1});
    bsy.AddKeyFrame(KeyFrameFloat{0.8f,1});
    c->controller=std::make_unique<AnimationController>(sk,c->resource);
    c->controller->AddLayer("Base",1); auto* tr=new BlendTree(); tr->AddClip("Bounce");
    c->controller->AddState("Base","Bounce",tr); c->controller->SetInitialState("Base","Bounce");
    AddDisp(*c,"Body",Vec3(0.2f,0.9f,0.3f),Vec3(),Vec3(0.8f,0.8f,0.8f),m_CubeMesh);
    AddDisp(*c,"Head",Vec3(1,0.9f,0.3f),Vec3(0,0.2f,0),Vec3(0.5f,0.4f,0.5f),m_SphereMesh);
    auto* p=c.get(); m_Characters.push_back(std::move(c)); return p;
}

AnimatedCharacter* AnimationDemoApp::MakeLooker() {
    auto c=std::make_unique<AnimatedCharacter>(); c->name="Looker"; c->worldPosition=Vec3(0,0,-4);
    c->skeleton=MakeHumanoid(); c->resource=MakeRes(c->skeleton);
    c->controller=std::make_unique<AnimationController>(c->skeleton,c->resource);
    c->controller->AddLayer("Base",1); c->controller->AddState("Base","Look",nullptr);
    c->controller->SetInitialState("Base","Look");
    c->constraintSolver=std::make_unique<ConstraintSolver>();
    int hi=c->skeleton->FindBoneIndex("Head");
    if(hi>=0){auto*lk=c->constraintSolver->AddConstraint<LookAtConstraint>("lk");lk->SetAffectedBone(hi);lk->AddTarget(ConstraintTarget::FromLocator("walker_hand"));lk->SetStrength(1);}
    int rf=c->skeleton->FindBoneIndex("RFore");
    if(rf>=0){auto*pt=c->constraintSolver->AddConstraint<PointConstraint>("pt");pt->SetAffectedBone(rf);pt->AddTarget(ConstraintTarget::FromLocator("walker_hand"));pt->SetStrength(0.8f);}
    AddDisp(*c,"Hips",Vec3(0.8f,0.3f,0.3f),Vec3(),Vec3(0.8f,0.6f,0.5f),m_CubeMesh);
    AddDisp(*c,"Spine",Vec3(0.9f,0.4f,0.3f),Vec3(),Vec3(0.7f,0.7f,0.4f),m_CubeMesh);
    AddDisp(*c,"Head",Vec3(1,0.6f,0.4f),Vec3(0,0.1f,0),Vec3(0.5f,0.4f,0.5f),m_SphereMesh);
    AddDisp(*c,"LArm",Vec3(0.9f,0.3f,0.2f),Vec3(),Vec3(0.3f,0.5f,0.3f),m_CylinderMesh);
    AddDisp(*c,"LFore",Vec3(0.8f,0.3f,0.2f),Vec3(),Vec3(0.25f,0.5f,0.25f),m_CylinderMesh);
    AddDisp(*c,"RArm",Vec3(0.9f,0.3f,0.2f),Vec3(),Vec3(0.3f,0.5f,0.3f),m_CylinderMesh);
    AddDisp(*c,"RFore",Vec3(0.8f,0.3f,0.2f),Vec3(),Vec3(0.25f,0.5f,0.25f),m_CylinderMesh);
    auto* p=c.get(); m_Characters.push_back(std::move(c)); return p;
}

void AnimationDemoApp::InitScene() {
    // 地面
    auto g=std::make_shared<GameObject>("Ground");
    g->GetTransform().SetPosition(0,-1.5f,0);
    auto* gm=g->AddComponent<MeshComponent>(); gm->SetMesh(std::make_shared<Mesh>(Mesh::CreatePlane(14,14)));
    gm->m_Color=Vec4(0.2f,0.25f,0.3f,1); m_Scene.AddObject(g);

    // 创建角色（骨骼 + 动画数据）
    MakeWalker(); MakeBouncer(); MakeLooker();
    std::cout<<"[AnimDemo] "<<m_Characters.size()<<" characters created ("
             <<"Walker:"<<m_Characters[0]->displays.size()
             <<" Bouncer:"<<m_Characters[1]->displays.size()
             <<" Looker:"<<m_Characters[2]->displays.size()<<" parts)\n";

    // 为每个骨骼显示创建 GameObject 并加入场景
    int totalObjects = 0;
    for (size_t ci = 0; ci < m_Characters.size(); ++ci) {
        auto& c = *m_Characters[ci];
        for (size_t di = 0; di < c.displays.size(); ++di) {
            auto& d = c.displays[di];
            auto obj = std::make_shared<GameObject>(
                c.name + "_bone" + std::to_string(di));
            auto* mc = obj->AddComponent<MeshComponent>();
            mc->SetMesh(d.mesh);
            mc->m_Color = Vec4(d.color.x, d.color.y, d.color.z, 1.0f);
            d.gameObject = obj;
            m_Scene.AddObject(obj);
            ++totalObjects;
        }
    }
    std::cout<<"[AnimDemo] Created "<<totalObjects<<" render objects for bones\n";
    std::cout<<"[AnimDemo] Scene total objects: "<<m_Scene.GetObjects().size()<<"\n";
}

void AnimationDemoApp::HandleInput(float32 dt){
    if(Input::IsKeyPressed(KeyCode::Escape))return;
    float sp=m_CameraSpeed*dt; Vec3 p=m_Camera.GetPosition(),f=m_Camera.GetForward(),r=m_Camera.GetRight();
    if(Input::IsKeyDown(KeyCode::W)){p.x+=f.x*sp;p.y+=f.y*sp;p.z+=f.z*sp;}
    if(Input::IsKeyDown(KeyCode::S)){p.x-=f.x*sp;p.y-=f.y*sp;p.z-=f.z*sp;}
    if(Input::IsKeyDown(KeyCode::A)){p.x-=r.x*sp;p.y-=r.y*sp;p.z-=r.z*sp;}
    if(Input::IsKeyDown(KeyCode::D)){p.x+=r.x*sp;p.y+=r.y*sp;p.z+=r.z*sp;}
    if(Input::IsKeyDown(KeyCode::Q))p.y-=sp;if(Input::IsKeyDown(KeyCode::E))p.y+=sp;
    m_Camera.SetPosition(p);
    float yw=m_Camera.GetYaw(),pt=m_Camera.GetPitch();
    if(Input::IsKeyDown(KeyCode::Left))yw-=50*dt;if(Input::IsKeyDown(KeyCode::Right))yw+=50*dt;
    if(Input::IsKeyDown(KeyCode::Up))pt+=50*dt;if(Input::IsKeyDown(KeyCode::Down))pt-=50*dt;
    pt=std::max(-89.0f,std::min(89.0f,pt)); m_Camera.SetRotation(pt,yw);
    if(Input::IsKeyPressed(KeyCode::Space))m_PauseAnimation=!m_PauseAnimation;
}

void AnimationDemoApp::UpdateChars(float32 dt){
    if(m_PauseAnimation)return; dt*=m_GlobalTimeScale;
    static float wa=0;
    for(auto& c:m_Characters){
        if(!c->skeleton||!c->controller)continue;
        if(c->name=="Walker"){wa+=dt*0.4f;c->worldPosition.x=-3+cos(wa)*3;c->worldPosition.z=sin(wa)*2;c->controller->SetFloat("Speed",1);}
        else c->controller->SetFloat("Speed",0);
        c->controller->Update(dt); c->controller->ApplyToSkeleton();
        if(c->constraintSolver){
            if(c->name=="Looker"&&m_Characters.size()>0){auto*w=m_Characters[0].get();if(w&&w->constraintSolver)c->constraintSolver->ImportExternalLocators(w->constraintSolver->GetLocatorsForExport());}
            PoseLocalData pl;pl.Resize(c->skeleton->GetBoneCount());
            for(size_t i=0;i<c->skeleton->GetBoneCount();++i){const Bone&b=c->skeleton->GetBone((int)i);pl.translations[i]=AnimationBlend::DecomposeTranslation(b.localPoseMatrix);pl.rotations[i]=AnimationBlend::DecomposeRotation(b.localPoseMatrix);pl.scales[i]=AnimationBlend::DecomposeScale(b.localPoseMatrix);}
            c->constraintSolver->Update(*c->skeleton,pl,dt);
            for(size_t i=0;i<c->skeleton->GetBoneCount();++i){Bone&b=c->skeleton->GetBone((int)i);AnimationPose::ComposeMatrix(pl.translations[i],AnimationBlend::ToEuler(pl.rotations[i]),pl.scales[i],b.localPoseMatrix);}
            c->skeleton->UpdateWorldPoses();
        }
    }
}

void AnimationDemoApp::UpdateDisplayTransforms() {
    for (auto& c : m_Characters) {
        if (!c->skeleton) continue;
        for (auto& d : c->displays) {
            if (!d.gameObject) continue;
            if (d.boneIndex < 0 || d.boneIndex >= (int)c->skeleton->GetBoneCount()) continue;

            const Bone& bone = c->skeleton->GetBone(d.boneIndex);

            // 世界矩阵 = 角色世界位置 * 骨骼世界矩阵 * 偏移 * 缩放
            glm::mat4 world = glm::translate(glm::mat4(1.0f),
                glm::vec3(c->worldPosition.x, c->worldPosition.y, c->worldPosition.z));
            glm::mat4 boneMat = glm::make_mat4(bone.currentPoseMatrix.Data());
            glm::mat4 off = glm::translate(glm::mat4(1.0f),
                glm::vec3(d.offset.x, d.offset.y, d.offset.z));
            glm::mat4 scl = glm::scale(glm::mat4(1.0f),
                glm::vec3(d.scale.x, d.scale.y, d.scale.z));
            glm::mat4 finalMat = world * boneMat * off * scl;

            // 从矩阵分解平移
            Vec3 pos(finalMat[3].x, finalMat[3].y, finalMat[3].z);
            // 从矩阵提取缩放（列向量长度）
            Vec3 s(
                glm::length(glm::vec3(finalMat[0])),
                glm::length(glm::vec3(finalMat[1])),
                glm::length(glm::vec3(finalMat[2]))
            );
            // 从矩阵提取旋转为欧拉角
            glm::mat4 rotMat = finalMat;
            rotMat[0] /= s.x; rotMat[1] /= s.y; rotMat[2] /= s.z;
            glm::quat q = glm::quat_cast(glm::mat3(rotMat));
            Vec3 rot = AnimationBlend::ToEuler(Quat(q.x, q.y, q.z, q.w));

            d.gameObject->GetTransform().SetPosition(pos);
            d.gameObject->GetTransform().SetRotation(rot);
            d.gameObject->GetTransform().SetScale(s);
        }
    }
}

void AnimationDemoApp::DrawUI(){
    if(!m_UIInitialized)return;
    ImGui::SetNextWindowPos(ImVec2(10,30),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400,600),ImGuiCond_FirstUseEver);
    ImGui::Begin("Anim Ctrl",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("WASD+QE:Move Arrows:Look Space:Pause");
    ImGui::Text("Scene objects: %zu", m_Scene.GetObjects().size());
    ImGui::Separator(); ImGui::Checkbox("Pause",&m_PauseAnimation);
    ImGui::SliderFloat("Speed",&m_GlobalTimeScale,0,3); ImGui::Separator();
    for(size_t i=0;i<m_Characters.size();++i){
        auto& c=*m_Characters[i];
        if(ImGui::CollapsingHeader(c.name.c_str())){
            ImGui::Text("Pos:(%.2f,%.2f,%.2f)",c.worldPosition.x,c.worldPosition.y,c.worldPosition.z);
            ImGui::Text("Bones:%zu Parts:%zu",c.skeleton?c.skeleton->GetBoneCount():0,c.displays.size());
            if(c.controller){auto info=c.controller->GetDebugInfo();for(auto& l:info.layers)ImGui::Text("State:%s(w=%.2f)",l.smDebug.cur.c_str(),l.weight);}
            if(c.constraintSolver){auto d=c.constraintSolver->GetDebugInfo();ImGui::Text("Constraints:%zu Locators:%zu",d.constraintCount,d.locatorCount);}
        }
    }
    ImGui::End();
}

void AnimationDemoApp::Run(){
    using namespace std::chrono;
    auto last=high_resolution_clock::now();
    // 检查初始化是否成功
    if (!m_Window || !m_3DShader || !m_MeshRenderer) {
        std::cerr << "[AnimDemo] Initialization failed, aborting.\n";
        return;
    }
    m_Camera.SetPosition(Vec3(8,6,8)); m_Camera.SetRotation(-30,-135);
    while(!m_Window->ShouldClose()){
        auto now=high_resolution_clock::now(); float dt=duration<float>(now-last).count(); last=now;
        if(dt>0.05f)dt=0.05f;
        m_Window->PollEvents(); HandleInput(dt); MemoryTracker::FrameStart();
        UpdateChars(dt);
        // 同步动画骨骼数据到渲染对象
        UpdateDisplayTransforms();
        std::vector<GameObject*> objs;
        for(auto& o:m_Scene.GetObjects())objs.push_back(o.get());
        auto* ctx=m_Window->GetContext();
        if(ctx){
            ctx->ClearColor(0.12f,0.14f,0.17f,1);
            m_3DShader->Bind();
            m_3DShader->SetMat4("u_View",m_Camera.GetViewMatrix().Data());
            m_3DShader->SetMat4("u_Projection",m_Camera.GetProjectionMatrix().Data());
            Vec3 a(0.3f,0.3f,0.35f),lc(1,1,1),lp(5,10,5),vp=m_Camera.GetPosition();
            m_3DShader->SetVec3("u_AmbientColor",&a.x); m_3DShader->SetVec3("u_LightColor",&lc.x);
            m_3DShader->SetFloat("u_LightIntensity",1); m_3DShader->SetVec3("u_ViewPos",&vp.x);
            m_3DShader->SetVec3("u_LightPos",&lp.x); m_3DShader->SetInt("u_HasTexture",0);
            m_MeshRenderer->RenderWithPVS(objs, m_Camera.GetPosition());
        }
        uint32 dc=ctx?ctx->GetAndResetDrawCallCount():0;
        if(m_UIInitialized&&UIManager::Get()){
            UIManager::Begin(); DrawUI(); m_PerfWindow.OnImGui(); m_ConsolePanel.OnImGui(); m_MemoryPanel.OnImGui();
            UIManager::End();
            ImGuiIO& io=ImGui::GetIO(); Input::SetBlockInput(io.WantCaptureMouse,io.WantCaptureKeyboard);
        }
        MemoryTracker::FrameEnd(); m_InputManager.OnUpdate();
        if(ctx)ctx->SwapBuffers();
    }
}

} // namespace
