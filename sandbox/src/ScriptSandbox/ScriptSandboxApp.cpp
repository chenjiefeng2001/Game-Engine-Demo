/**
 * @file ScriptSandboxApp.cpp
 * @brief Editor Workflow v1.1 — Milestone 4 DX
 *
 * ⚠️ Include order: nlohmann/json.hpp MUST come before spdlog/fmt headers
 *    to avoid MSVC macro conflicts. See M4-E in docs.
 */

// ── Phase 0: nlohmann FIRST (before any spdlog/fmt pollution) ──
#include <nlohmann/json.hpp>

// ── Phase 1: Own header + standard library ──
#include "ScriptSandboxApp.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

// ── Phase 2: Content headers (safe now, nlohmann already established) ──
#include <Engine/Core/Content/ContentAsset.h>
#include <Engine/Core/Content/SceneSerializerV1.h>

// ── Phase 3: spdlog chain (AFTER nlohmann) ──
#include <Engine/Core/Resources/ResourceManager.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/Log.h>
#include <Engine/Scripting/LuaEngine.h>
#include <Engine/Scripting/ScriptAPI.h>
#include <Engine/Scripting/GameplayAPI.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <imgui.h>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

// ═══ EditorState ═══

struct ScriptSandboxApp::EditorState {
    uint32_t selected = 0;
    std::unique_ptr<ContentRegistry> registry;
    std::unordered_map<uint32_t, EntityContentBinding> bindings;
    static constexpr const char* sceneFile = "sandbox_scene.json";
    static constexpr const char* manifestFile = "sandbox_manifest.json";
    bool sceneLoaded = false;

    // M4-B script editor
    char scriptBuf[16384] = {};
    bool scriptDirty = false;
    std::string scriptPath;
    bool scriptPanelOpen = false;

    // Console
    char cmdBuf[256] = {};
    std::vector<std::string> scrollback;

    // Asset Browser
    char assetSearch[128] = {};
    int assetFilter = 0;

    void Log(const std::string& msg) { scrollback.push_back(msg); }
};

// Helper accessors (used by test_content)
static EditorState& ED(ScriptSandboxApp*) { throw std::runtime_error("stub"); }

// ═══ Constructor / Destructor ═══

ScriptSandboxApp::ScriptSandboxApp(IGraphicsFactory& f)
    : Application(f), m_TexMgr(std::make_unique<TextureManager>(f))
{ m_Ed = std::make_unique<EditorState>(); }

ScriptSandboxApp::~ScriptSandboxApp() = default;

void ScriptSandboxApp::AppendLog(const std::string& s) { m_Ed->Log(s); }

// ═══ OnStartup (M4-A: empty scene default) ═══

void ScriptSandboxApp::OnStartup() {
    m_InputMgr.Init(&GetWindow());
    if (!g_inputOwner)
        g_inputOwner = std::make_unique<GLFWScriptInputProvider>();
    Scripting::GameplayAPI::SetInputProvider(g_inputOwner.get());

    if (m_Ed->registry->LoadManifest(m_Ed->manifestFile))
        AppendLog("[registry] manifest loaded (" + std::to_string(m_Ed->registry->Count()) + ")");
    else
        AppendLog("[registry] fresh session");

    auto* ctx = GetWindow().GetContext();
    m_Batch = m_Factory.CreateSpriteBatch(*ctx);
    m_BatchShader = m_Factory.CreateShader(
        "assets/shaders/sprite_batch.vert","assets/shaders/sprite_batch.frag");
    m_Tex = m_TexMgr->Load("assets/textures/test.png");

    // M4-A: Only restore saved scene, never auto-load scripts
    if (std::filesystem::exists(m_Ed->scenePath)) {
        AppendLog("[scene] saved scene found - restoring...");
        LoadScene();
    } else {
        AppendLog("Welcome! Empty scene ready.");
        AppendLog("Use Hierarchy to create entities.");
        AppendLog("Use Asset Browser to import and assign assets.");
    }
}

void ScriptSandboxApp::NewScene() {
    for (auto& [h,b] : m_Ed->bindings)
        Scripting::GameplayAPI::HandleDestroy(h);
    m_Ed->bindings.clear(); m_Scene.Clear(); m_Ed->selected=0;
    m_Player.Shutdown(); m_Ed->sceneLoaded=false;
    AppendLog("[scene] New empty scene");
}

void ScriptSandboxApp::OnUpdate(float32 dt) {
    if (Input::IsKeyPressed(KeyCode::F5)) {
        AppendLog("[reload] F5");
        if (!m_Ed->scriptPath.empty() && m_Player.Reload())
            AppendLog("[reload] OK"); else AppendLog("[reload] FAILED");
    }
    if (m_Ed->sceneLoaded || m_Player.IsValid())
        m_Player.OnUpdate(dt);
}

void ScriptSandboxApp::OnRender() {
    auto* ctx = GetWindow().GetContext();
    ctx->ClearColor(0.08f, 0.09f, 0.12f, 1.f);
    m_Shader->Bind();
    m_Shader->SetMat4("u_ViewProjection", m_Camera.GetViewProjectionMatrixPtr());
    m_Batch->Begin(m_Tex);
    {   SpriteData c; c.transform.x=-3.f; c.transform.y=0.f;
        c.transform.scaleX=.8f; c.transform.scaleY=.8f; c.colorB=1.f;
        m_Batch->Draw(c); }
    float px=2.f, py=0.f;
    Scripting::GameplayAPI::HandleGetPosition(1, px, py);
    {   SpriteData p; p.transform.x=px; p.transform.y=py;
        p.transform.scaleX=.6f; p.transform.scaleY=.6f; p.colorG=1.f;
        m_Batch->Draw(p); }
    m_Batch->End();
}

// ═══ Hierarchy (M4-D) ═══

void ScriptSandboxApp::DrawHierarchyPanel() {
    ImGui::Begin("Hierarchy");
    if (ImGui::Button("Create Entity")) {
        auto h = Scripting::GameplayAPI::HandleSpawn(
            "Entity_"+std::to_string(Scripting::GameplayAPI::HandleCount()+1));
        m_Ed->bindings[h]={}; m_Ed->selected=h;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && m_Ed->selected) {
        Scripting::GameplayAPI::HandleDestroy(m_Ed->selected);
        m_Ed->bindings.erase(m_Ed->selected); m_Ed->selected=0;
    }
    ImGui::Separator();
    for (auto& o : m_Scene.GetObjects()) {
        uint32_t h=0;
        for (auto& [hh,b] : m_Ed->bindings)
            if (Scripting::GameplayAPI::HandleGetName(hh)==o->GetName()){h=hh;break;}
        if (ImGui::Selectable((o->GetName()+" ["+std::to_string(h)+"]").c_str(),
                              m_Ed->selected==h)) m_Ed->selected=h;
    }
    ImGui::End();
}

// ═══ Inspector ═══

void ScriptSandboxApp::DrawInspectorPanel() {
    ImGui::Begin("Inspector");
    if (!m_Ed->selected){ImGui::TextDisabled("No selection");ImGui::End();return;}
    float x,y,z; x=y=z=0;
    Scripting::GameplayAPI::HandleGetPosition(m_Ed->selected,x,y,z);
    float pos[3]={x,y,z};
    if(ImGui::DragFloat3("Position",pos,.05f))
        Scripting::GameplayAPI::HandleSetPosition(m_Ed->selected,pos[0],pos[1],pos[2]);

    auto it=m_Ed->bindings.find(m_Ed->selected);
    if(it==m_Ed->bindings.end())
        it=m_Ed->bindings.emplace(m_Ed->selected,Content::EntityContentBinding{}).first;
    auto& b=it->second;
    ImGui::Separator();
    ImGui::Text("Sprite: %s",b.spriteGuid.IsNull()?"<none>":
               m_Registry.ResolvePath(b.spriteGuid).c_str());
    ImGui::Text("Script: %s",b.scriptGuid.IsNull()?"<none>":
               m_Registry.ResolvePath(b.scriptGuid).c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Assign via Asset Browser double-click");
    ImGui::End();
}

// ═══ Asset Browser ═══

void ScriptSandboxApp::DrawAssetBrowser() {
    ImGui::SetNextWindowSize(ImVec2(360,320),ImGuiCond_FirstUseEver);
    ImGui::Begin("Asset Browser");
    ImGui::InputText("##s",m_Ed->assetSearch,sizeof(m_Ed->assetSearch));
    ImGui::SameLine();
    const char* fl[]={"All","Texture","Script"};
    for(int i=0;i<3;++i){if(i>0)ImGui::SameLine();if(ImGui::Button(fl[i]))m_Ed->assetFilter=i;}
    ImGui::Separator();
    auto entries=m_Registry.GetAllEntries();
    std::string q(m_Ed->assetSearch);
    int shown=0;
    for(auto&e:entries){
        bool ok=q.empty()||e.path.find(q)!=std::string::npos;
        bool tok=(m_Ed->assetFilter==0)||
                 (m_Ed->assetFilter==1&&e.type==Content::AssetType::Texture)||
                 (m_Ed->assetFilter==2&&e.type==Content::AssetType::Script);
        if(!ok||!tok)continue;
        ++shown;
        ImGui::PushID(static_cast<int>(e.guid.low));
        if(ImGui::Selectable(e.path.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick)){
            if(ImGui::IsMouseDoubleClicked(0)&&m_Ed->selected){
                if(e.type==Content::AssetType::Texture)
                    m_Ed->bindings[m_Ed->selected].spriteGuid=e.guid;
                else
                    m_Ed->bindings[m_Ed->selected].scriptGuid=e.guid;
            }
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("%d shown",shown);
    ImGui::End();
}

// ═══ Console ═══

void ScriptSandboxApp::DrawConsolePanel() {
    ImGui::SetNextWindowSize(ImVec2(520,280),ImGuiCond_FirstUseEver);
    ImGui::Begin("Console");
    ImGui::Text("api=%.1f entities=%u sel=%u",
                Scripting::ScriptAPI::GetApiVersion(),
                Scripting::GameplayAPI::HandleCount(),m_Ed->selected);
    ImGui::Separator();
    ImGui::BeginChild("#log",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()),false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for(auto&l:m_Ed->scrollback)ImGui::TextUnformatted(l.c_str());
    if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY())ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    bool doExec=ImGui::InputText("##cmd",m_Ed->cmdBuf,sizeof(m_Ed->cmdBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    doExec|=ImGui::Button("Run");
    if(doExec&&m_Ed->cmdBuf[0]){
        AppendLog("> "+std::string(m_Ed->cmdBuf));
        if(m_Player.Execute(m_Ed->cmdBuf))AppendLog("[ok]");
        else AppendLog("[err] "+m_Player.GetEngine()->GetLastError().message);
        m_Ed->cmdBuf[0]=0;
    }
    ImGui::End();
}

// ═══ Script Editor MVP ═══

void ScriptSandboxApp::DrawScriptEditor() {
    if(!m_Ed->scriptPanelOpen)return;
    ImGui::SetNextWindowSize(ImVec2(640,480),ImGuiCond_FirstUseEver);
    ImGui::Begin("Script Editor",&m_Ed->scriptPanelOpen);
    ImGui::Text("%s%s",m_Ed->scriptPath.c_str(),m_Ed->scriptDirty?" *":"");
    ImGui::Separator();
    ImGui::InputTextMultiline("##src",m_Ed->scriptBuf,sizeof(m_Ed->scriptBuf),
                              ImVec2(-1,-ImGui::GetFrameHeightWithSpacing()*2),
                              ImGuiInputTextFlags_AllowTabInput);
    if(ImGui::Button("Save")){
        std::ofstream f(m_Ed->scriptPath,std::ios::binary|std::ios::trunc);
        f.write(m_Ed->scriptBuf,strlen(m_Ed->scriptBuf));f.close();
        AppendLog("[editor] saved");
    }
    ImGui::SameLine();
    if(ImGui::Button("Save+Reload")){
        SaveScriptEditorImpl();
        if(m_Player.Reload())AppendLog("[editor+reload] OK");
        else AppendLog("[editor+reload] FAILED");
    }
    ImGui::End();
}

void ScriptSandboxApp::OpenScriptEditor(const std::string& path) {
    m_Ed->scriptPath=path;
    std::ifstream f(path,std::ios::binary);
    if(!f.is_open()){AppendLog("[editor] cannot open: "+path);return;}
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    size_t copyLen=content.size()<sizeof(m_Ed->scriptBuf)-1?content.size():sizeof(m_Ed->scriptBuf)-1;
    memcpy(m_Ed->scriptBuf,content.c_str(),copyLen);
    m_Ed->scriptBuf[copyLen]='\0';
    m_Ed->scriptDirty=false;m_Ed->scriptPanelOpen=true;
}

void ScriptSandboxApp::SaveScriptEditor() {
    if(m_Ed->scriptPath.empty())return;
    std::ofstream f(m_Ed->scriptPath,std::ios::binary|std::ios::trunc);
    f.write(m_Ed->scriptBuf,strlen(m_Ed->scriptBuf));f.close();
}
