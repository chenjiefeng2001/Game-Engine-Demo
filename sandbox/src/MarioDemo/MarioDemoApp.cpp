#include "MarioDemoApp.h"
#include <Engine/OpenAL/OpenALAudioEngine.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/UIManager.h>
#include <iostream>
#include <cmath>
#include <chrono>

namespace Engine {

MarioDemoApp::MarioDemoApp(IGraphicsFactory& factory)
    : m_Factory(factory), m_TextureManager(factory), m_SceneRenderer(factory,m_TextureManager), m_Scene("MarioDemo")
{
    m_Window=m_Factory.CreateWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"Super Mario Demo");
    m_InputManager.Init(m_Window.get());
    m_SceneRenderer.SetRenderContext(*m_Window->GetContext());
    auto shader=m_Factory.CreateShader("assets/shaders/sprite_batch.vert","assets/shaders/sprite_batch.frag");
    m_SceneRenderer.SetShader(shader);
    float as=float(WINDOW_WIDTH)/WINDOW_HEIGHT;
    m_ViewHeight=13.0f; m_ViewWidth=m_ViewHeight*as;
    m_Camera=OrthographicCamera(0,m_ViewWidth,0,m_ViewHeight);
    m_SceneRenderer.SetCamera(&m_Camera);
    auto lt=[&](const std::string& n){return m_TextureManager.Load(std::string("assets/textures/mario/")+n);};
    m_Tex["ground"]=lt("ground.png"); m_Tex["brick"]=lt("brick.png"); m_Tex["question"]=lt("question.png");
    m_Tex["coin"]=lt("coin.png"); m_Tex["goomba"]=lt("goomba.png"); m_Tex["cloud"]=lt("cloud.png");
    m_Tex["ms"]=lt("mario_stand.png"); m_Tex["mw1"]=lt("mario_walk1.png"); m_Tex["mw2"]=lt("mario_walk2.png"); m_Tex["mj"]=lt("mario_jump.png");
    m_Tex["flagpole"]=lt("flagpole.png"); m_Tex["castle"]=lt("castle.png");
    try{m_Audio=std::make_shared<OpenALAudioEngine>();if(m_Audio&&!m_Audio->Init()){m_Audio.reset();}}catch(...){}
    auto ac=[&](const std::string& p){auto c=std::make_shared<AudioClip>();c->LoadFromFile(std::string("assets/sounds/")+p);return c;};
    m_SndJump=ac("jump.wav");m_SndCoin=ac("coin.wav");m_SndStomp=ac("stomp.wav");m_SndDie=ac("death.wav");
    // ── 初始化 ImGui UI ──
    InitUI();
    // ── 注册控制台命令 ──
    InitConsoleCommands();
    InitBGM();
    BuildLevel_1_1();
}

bool MarioDemoApp::InitUI() {
    auto ui = m_Factory.CreateUIManager();
    if (!ui) return false;
    m_UIInitialized = UIManager::Init(std::move(ui), m_Window->GetNativeHandle(), m_Window->GetContext());
    return m_UIInitialized;
}

void MarioDemoApp::InitConsoleCommands() {
    auto& reg = ConsoleCommandRegistry::Instance();
    reg.Register({"mario_god", "Mario 无敌模式", "mario_god",
        [this](const std::vector<std::string>&, std::string& out) {
            static bool god = false; god = !god;
            out = god ? "^2Mario God mode ON^7" : "^1Mario God mode OFF^7";
        }
    });
    reg.Register({"mario_level", "切换关卡", "mario_level [1|2]",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1 && args[1] == "2") { BuildLevel_1_2(); out = "^2Switched to Level 1-2^7"; }
            else { BuildLevel_1_1(); out = "^2Switched to Level 1-1^7"; }
        }
    });
    reg.Register({"mario_status", "显示 Mario 游戏状态", "mario_status",
        [this](const std::vector<std::string>&, std::string& out) {
            out = "^5=== Mario Status ===^7\n";
            out += "  Level: 1-" + std::string(m_Level == 0 ? "1" : "2") + "\n";
            out += "  Score: " + std::to_string(m_Score) + "\n";
            out += "  Lives: " + std::to_string(m_Lives) + "\n";
            out += "  Coins: " + std::to_string(m_CoinsCollected) + "\n";
            out += "  State: " + std::string(m_State == PlayerState::Dead ? "DEAD" : 
                      m_State == PlayerState::LevelComplete ? "COMPLETE" : "PLAYING") + "\n";
        }
    });
}

static void AT(Scene& s,std::shared_ptr<IPhysicsWorld> w,float x,float y,std::shared_ptr<Texture> t){
    auto o=std::make_shared<GameObject>("T");o->GetTransform().SetPosition(x,y,0);
    if(t)o->GetSprite().SetTexture(t);o->GetSprite().SetSortingLayer(2);
    auto* p=o->AddComponent<PhysicsComponent>();BodyDef b;b.type=BodyType::Static;b.shape.type=ShapeType::Box;b.shape.boxSize={0.5f,0.5f};b.material.friction=0.5f;
    p->CreateBody(w,b);p->SyncTransformToPhysics({x,y},0);s.AddObject(o);
}

static void AG(Scene& s,std::shared_ptr<IPhysicsWorld> w,float x,float y,std::shared_ptr<Texture> t,auto& ev){
    auto e=std::make_shared<GameObject>("G");e->GetTransform().SetPosition(x,y,0);
    if(t)e->GetSprite().SetTexture(t);e->GetSprite().SetSortingLayer(8);
    auto* ep=e->AddComponent<PhysicsComponent>();BodyDef b;b.type=BodyType::Dynamic;b.shape.type=ShapeType::Box;b.shape.boxSize={0.4f,0.4f};b.material.density=1;b.material.friction=0.2f;b.fixedRotation=true;
    ep->CreateBody(w,b);ep->SyncTransformToPhysics({x,y},0);s.AddObject(e);
    ev.push_back({e,true,-1,1.2f});
}

static void AddWall2(std::shared_ptr<IPhysicsWorld> w, float px, float py, float hw, float hh) {
    BodyDef b; b.type=BodyType::Static; b.position={px,py}; b.shape.type=ShapeType::Box; b.shape.boxSize={hw,hh}; w->CreateBody(b);
}

static void AC(Scene& s,float x,float y,std::shared_ptr<Texture> t,std::vector<std::shared_ptr<GameObject>>& cv){
    auto o=std::make_shared<GameObject>("C");o->GetTransform().SetPosition(x,y,0);o->GetTransform().SetScale(Vec3(0.75f,1,1));
    if(t)o->GetSprite().SetTexture(t);o->GetSprite().SetSortingLayer(5);s.AddObject(o);cv.push_back(o);
}

void MarioDemoApp::BuildLevel_1_1(){
    m_Scene.Clear();m_Enemies.clear();m_Coins.clear();m_Player.reset();
    m_Level=0;m_LevelW=80;m_LevelH=13;
    m_Physics=std::make_shared<Box2DPhysicsWorld>(Vec2(0,-20));
    for(int c=0;c<80;++c)AT(m_Scene,m_Physics,c+0.5f,0.5f,m_Tex["ground"]);
    AT(m_Scene,m_Physics,16.5f,10.5f,m_Tex["question"]);
    AT(m_Scene,m_Physics,20.5f,10.5f,m_Tex["question"]);
    for(int c=21;c<=24;++c)AT(m_Scene,m_Physics,c+0.5f,10.5f,m_Tex["brick"]);
    AT(m_Scene,m_Physics,22.5f,8.5f,m_Tex["brick"]);AT(m_Scene,m_Physics,23.5f,8.5f,m_Tex["brick"]);
    AT(m_Scene,m_Physics,25.5f,12.5f,m_Tex["question"]);
    for(int c=37;c<=40;++c)AT(m_Scene,m_Physics,c+0.5f,4.5f,m_Tex["ground"]);
    for(int c=38;c<=40;++c)AT(m_Scene,m_Physics,c+0.5f,5.5f,m_Tex["ground"]);
    for(int c=39;c<=40;++c)AT(m_Scene,m_Physics,c+0.5f,6.5f,m_Tex["ground"]);
    AT(m_Scene,m_Physics,40.5f,7.5f,m_Tex["ground"]);
    auto ad=[&](float x,float y,const std::string& k,float sx,float sy,float a){auto o=std::make_shared<GameObject>("D");o->GetTransform().SetPosition(x,y,0);o->GetTransform().SetScale(Vec3(sx,sy,1));if(auto t=m_Tex.find(k)!=m_Tex.end()?m_Tex[k]:nullptr)o->GetSprite().SetTexture(t);o->GetSprite().SetColor(1,1,1,a);o->GetSprite().SetSortingLayer(0);m_Scene.AddObject(o);};
    ad(12,12,"cloud",3,1.5f,0.6f);ad(28,12,"cloud",3,1.5f,0.6f);ad(50,12,"cloud",4,2,0.6f);
    m_Player=std::make_shared<GameObject>("Mario");m_Player->GetTransform().SetPosition(4,10.5f,0);
    if(auto t=m_Tex.find("ms")!=m_Tex.end()?m_Tex["ms"]:nullptr)m_Player->GetSprite().SetTexture(t);
    m_Player->GetSprite().SetSortingLayer(10);
    auto* pp=m_Player->AddComponent<PhysicsComponent>();BodyDef b;b.type=BodyType::Dynamic;b.shape.type=ShapeType::Box;b.shape.boxSize={0.35f,0.45f};b.material.density=1;b.material.friction=0.2f;b.fixedRotation=true;
    pp->CreateBody(m_Physics,b);pp->SyncTransformToPhysics({4,10.5f},0);m_Scene.AddObject(m_Player);
    m_State=PlayerState::Idle;m_OnGround=false;m_DeathTimer=0;m_InvTimer=0;m_LevelCompleteTimer=0;
    AG(m_Scene,m_Physics,30.5f,10.5f,m_Tex["goomba"],m_Enemies);AG(m_Scene,m_Physics,45.5f,10.5f,m_Tex["goomba"],m_Enemies);
    AC(m_Scene,17.5f,10.5f,m_Tex["coin"],m_Coins);AC(m_Scene,32.5f,10.5f,m_Tex["coin"],m_Coins);AC(m_Scene,33.5f,10.5f,m_Tex["coin"],m_Coins);AC(m_Scene,50.5f,10.5f,m_Tex["coin"],m_Coins);
    // ── 终点旗杆与城堡 ──
    AddWall2(m_Physics,77.0f,6.5f,0.15f,6.0f); // 旗杆碰撞体
    {   auto flag=std::make_shared<GameObject>("FlagPole");
        flag->GetTransform().SetPosition(77.0f,6.5f,0);
        if(m_Tex["flagpole"])flag->GetSprite().SetTexture(m_Tex["flagpole"]);
        flag->GetSprite().SetSortingLayer(10);
        m_Scene.AddObject(flag);
    }
    {   auto castl=std::make_shared<GameObject>("Castle");
        castl->GetTransform().SetPosition(79.0f,1.5f,0);
        castl->GetTransform().SetScale(Vec3(2.0f,2.0f,1));
        if(m_Tex["castle"])castl->GetSprite().SetTexture(m_Tex["castle"]);
        castl->GetSprite().SetSortingLayer(3);
        m_Scene.AddObject(castl);
    }
    std::cout<<"[Mario] 1-1 ready\\n";
}

void MarioDemoApp::BuildLevel_1_2(){
    m_Scene.Clear();m_Enemies.clear();m_Coins.clear();m_Player.reset();
    m_Level=1;m_LevelW=80;m_LevelH=13;
    m_Physics=std::make_shared<Box2DPhysicsWorld>(Vec2(0,-20));
    for(int c=0;c<80;++c)AT(m_Scene,m_Physics,c+0.5f,0.5f,m_Tex["brick"]);
    for(int c=10;c<=15;++c)AT(m_Scene,m_Physics,c+0.5f,3.5f,m_Tex["ground"]);
    for(int c=8;c<=12;++c)AT(m_Scene,m_Physics,c+0.5f,7.5f,m_Tex["brick"]);
    AT(m_Scene,m_Physics,10.5f,9.5f,m_Tex["question"]);
    for(int c=20;c<=30;++c)AT(m_Scene,m_Physics,c+0.5f,10.5f,m_Tex["ground"]);
    AT(m_Scene,m_Physics,22.5f,12.5f,m_Tex["question"]);AT(m_Scene,m_Physics,26.5f,12.5f,m_Tex["brick"]);
    for(int c=35;c<=40;++c)AT(m_Scene,m_Physics,c+0.5f,4.5f,m_Tex["brick"]);
    for(int c=42;c<=48;++c)AT(m_Scene,m_Physics,c+0.5f,8.5f,m_Tex["ground"]);
    AT(m_Scene,m_Physics,45.5f,10.5f,m_Tex["question"]);
    for(int c=55;c<=60;++c)AT(m_Scene,m_Physics,c+0.5f,2.5f,m_Tex["ground"]);
    for(int c=56;c<=60;++c)AT(m_Scene,m_Physics,c+0.5f,3.5f,m_Tex["ground"]);
    for(int c=57;c<=60;++c)AT(m_Scene,m_Physics,c+0.5f,4.5f,m_Tex["ground"]);
    for(int c=58;c<=60;++c)AT(m_Scene,m_Physics,c+0.5f,5.5f,m_Tex["ground"]);
    m_Player=std::make_shared<GameObject>("Mario");m_Player->GetTransform().SetPosition(4,10.5f,0);
    if(auto t=m_Tex.find("ms")!=m_Tex.end()?m_Tex["ms"]:nullptr)m_Player->GetSprite().SetTexture(t);
    m_Player->GetSprite().SetSortingLayer(10);
    auto* pp=m_Player->AddComponent<PhysicsComponent>();BodyDef b2;b2.type=BodyType::Dynamic;b2.shape.type=ShapeType::Box;b2.shape.boxSize={0.35f,0.45f};b2.material.density=1;b2.material.friction=0.2f;b2.fixedRotation=true;
    pp->CreateBody(m_Physics,b2);pp->SyncTransformToPhysics({4,10.5f},0);m_Scene.AddObject(m_Player);
    m_State=PlayerState::Idle;m_OnGround=false;m_DeathTimer=0;m_InvTimer=0;m_LevelCompleteTimer=0;
    AG(m_Scene,m_Physics,18.5f,10.5f,m_Tex["goomba"],m_Enemies);AG(m_Scene,m_Physics,35.5f,10.5f,m_Tex["goomba"],m_Enemies);
    AG(m_Scene,m_Physics,50.5f,10.5f,m_Tex["goomba"],m_Enemies);AG(m_Scene,m_Physics,65.5f,10.5f,m_Tex["goomba"],m_Enemies);
    AC(m_Scene,11.5f,7.5f,m_Tex["coin"],m_Coins);AC(m_Scene,21.5f,10.5f,m_Tex["coin"],m_Coins);AC(m_Scene,36.5f,10.5f,m_Tex["coin"],m_Coins);
    AC(m_Scene,44.5f,10.5f,m_Tex["coin"],m_Coins);AC(m_Scene,46.5f,10.5f,m_Tex["coin"],m_Coins);
    // ── 终点旗杆与城堡 ──
    AddWall2(m_Physics,77.0f,6.5f,0.15f,6.0f);
    {   auto flag=std::make_shared<GameObject>("FlagPole");
        flag->GetTransform().SetPosition(77.0f,6.5f,0);
        if(m_Tex["flagpole"])flag->GetSprite().SetTexture(m_Tex["flagpole"]);
        flag->GetSprite().SetSortingLayer(10);
        m_Scene.AddObject(flag);
    }
    {   auto castl=std::make_shared<GameObject>("Castle");
        castl->GetTransform().SetPosition(79.0f,1.5f,0);
        castl->GetTransform().SetScale(Vec3(2.0f,2.0f,1));
        if(m_Tex["castle"])castl->GetSprite().SetTexture(m_Tex["castle"]);
        castl->GetSprite().SetSortingLayer(3);
        m_Scene.AddObject(castl);
    }
    std::cout<<"[Mario] 1-2 ready (underground)\\n";
}

void MarioDemoApp::ResetLevel(){
    if(m_BgmSource){m_BgmSource->Stop();if(m_Audio)m_Audio->DestroySource(m_BgmSource.get());}
    m_BgmSource.reset();m_BgmBuffer.reset();
    if(m_Level==0)BuildLevel_1_1();else BuildLevel_1_2();
    InitBGM();
}
void MarioDemoApp::Die(){m_State=PlayerState::Dead;m_DeathTimer=2;if(m_SndDie)Audio::PlayOneShot(*m_Audio,*m_SndDie);}

void MarioDemoApp::InitBGM(){
    if(!m_Audio)return;
    const int sr=22050;
    std::vector<uint8_t>pcm;

    // ── 超级马里奥主题旋律（简化版，C大调） ──
    // 每个音符: {频率(hz), 持续时间(秒)}
    struct Note{float freq;float dur;};
    Note melody[]={
        // 第一部分
        {659,0.12},{659,0.12},{0,0.12},{659,0.12},{0,0.12},{523,0.12},{659,0.12},{0,0.12},
        {784,0.12},{0,0.24},{392,0.12},{0,0.24},
        {523,0.12},{0,0.24},{392,0.12},{0,0.24},{330,0.12},{0,0.24},{0,0.24},
        // 第二部分
        {659,0.12},{659,0.12},{0,0.12},{659,0.12},{0,0.12},{523,0.12},{659,0.12},{0,0.12},
        {784,0.12},{0,0.24},{392,0.12},{0,0.24},
        {523,0.12},{0,0.24},{392,0.12},{0,0.24},{330,0.12},{0,0.24},{0,0.24},
        // 桥段
        {1047,0.12},{0,0.12},{988,0.12},{0,0.12},{1047,0.12},{0,0.12},{784,0.12},{0,0.12},
        {740,0.12},{0,0.12},{659,0.12},{0,0.12},{659,0.12},{0,0.24},
        {784,0.12},{0,0.12},{988,0.12},{0,0.12},{1047,0.12},{0,0.12},{784,0.12},{0,0.12},
        {740,0.12},{0,0.12},{659,0.12},{0,0.24},{0,0.24},{0,0.24},
    };

    for(const auto& n:melody){
        int numSamples=int(sr*n.dur);
        for(int i=0;i<numSamples;++i){
            float t=float(i)/sr;
            float v=0;
            if(n.freq>0){
                // 方波 + 正弦混合，更接近 8-bit 风格
                float sq=std::sin(6.2832f*n.freq*t)>=0?0.3f:-0.3f;
                float si=std::sin(6.2832f*n.freq*t)*0.15f;
                v=sq+si;
                // 快速音量衰减（模拟老式音频）
                float env=1.0f-(float(i)/numSamples)*0.3f;
                v*=env;
            }
            int16_t s=int16_t(v*14000);
            pcm.push_back(uint8_t(s&0xFF));
            pcm.push_back(uint8_t((s>>8)&0xFF));
        }
    }

    AudioClipInfo inf;
    inf.format=AudioFormat::Mono16;
    inf.sampleRate=sr;
    inf.dataSize=int32(pcm.size());
    inf.duration=float(pcm.size())/float(sr*2);
    m_BgmBuffer=m_Audio->CreateBuffer(pcm.data(),inf.dataSize,inf);
    if(m_BgmBuffer){
        m_BgmSource=m_Audio->CreateSource();
        if(m_BgmSource){
            m_BgmSource->SetLooping(true);
            m_BgmSource->SetVolume(0.15f);
            m_BgmSource->Play(m_BgmBuffer);
        }
    }
}

void MarioDemoApp::InitLevelCompleteFanfare(){
    if(!m_Audio)return;
    const int sr=22050;
    std::vector<uint8_t>pcm;

    // ── 关卡胜利音效（马里奥降旗子） ──
    float notes[]={988,784,659,523,659,784,988,1047,1175,1319};
    for(float f:notes){
        int n=int(sr*0.1f);
        for(int i=0;i<n;++i){
            float t=float(i)/sr;
            float v=(f>0)?(std::sin(6.2832f*f*t)*0.25f+std::sin(12.566f*f*t)*0.08f):0;
            float env=1.0f-(float(i)/n)*0.4f;
            v*=env;
            int16_t s=int16_t(v*16000);
            pcm.push_back(uint8_t(s&0xFF));
            pcm.push_back(uint8_t((s>>8)&0xFF));
        }
    }

    AudioClipInfo inf;
    inf.format=AudioFormat::Mono16;
    inf.sampleRate=sr;
    inf.dataSize=int32(pcm.size());
    inf.duration=float(pcm.size())/float(sr*2);
    auto fanfareBuffer=m_Audio->CreateBuffer(pcm.data(),inf.dataSize,inf);
    // 复用 BGM 音源播放通关音效（不循环）
    if(m_BgmSource&&fanfareBuffer){
        m_BgmSource->SetLooping(false);
        m_BgmSource->SetVolume(0.5f);
        m_BgmSource->Play(fanfareBuffer);
    }
}

void MarioDemoApp::GoToNextLevel(){
    // 清理旧 BGM 资源
    if(m_BgmSource){
        m_BgmSource->Stop();
        if(m_Audio)m_Audio->DestroySource(m_BgmSource.get());
    }
    m_BgmSource.reset();
    m_BgmBuffer.reset();
    m_LevelCompleteTimer=0;
    // 根据当前关卡进入下一关
    if(m_Level==0){
        std::cout<<"[Mario] 🎉 1-1 通关！进入 1-2...\n";
        BuildLevel_1_2();
    }else{
        std::cout<<"[Mario] 🎉 恭喜通关所有关卡！重新开始...\n";
        BuildLevel_1_1();
    }
    // 重新开始播放 BGM
    InitBGM();
}

void MarioDemoApp::Run(){
    using namespace std::chrono;auto last=high_resolution_clock::now();
    while(!m_Window->ShouldClose()&&!m_ShouldClose){
        auto now=high_resolution_clock::now();float dt=duration<float>(now-last).count();last=now;if(dt>0.05f)dt=0.05f;
        m_Window->PollEvents();

        // ── 控制台切换（~ 键） ──
        if (Input::IsKeyPressed(KeyCode::GraveAccent)) {
            m_ConsolePanel.ToggleVisibility();
        }
        bool consoleCaptures = m_ConsolePanel.IsCapturingInput();
        Input::SetBlockInput(consoleCaptures, consoleCaptures);

        // ── 内存帧追踪 ──
        MemoryTracker::FrameStart();
        if(Input::IsKeyPressed(KeyCode::Num1))BuildLevel_1_1();
        if(Input::IsKeyPressed(KeyCode::Num2))BuildLevel_1_2();
        if(Input::IsKeyPressed(KeyCode::R))ResetLevel();
        if(Input::IsKeyPressed(KeyCode::Escape))m_ShouldClose=true;
        static float acc=0;acc+=dt;while(acc>=1.f/60.f){m_Physics->Step(1.f/60.f,8,3);acc-=1.f/60.f;}
        std::function<void(GameObject&)> sy=[&](GameObject& o){if(auto* p=o.GetComponent<PhysicsComponent>())if(p->HasBody()){Vec2 pos;float ang;p->SyncPhysicsToTransform(pos,ang);o.GetTransform().SetPosition(pos.x,pos.y,0);o.GetTransform().SetRotation(0,0,ang);}for(auto& c:o.GetChildren())sy(*c);};
        for(auto& o:m_Scene.GetObjects())sy(*o);
        if(m_Player&&m_State!=PlayerState::Dead&&m_State!=PlayerState::LevelComplete){
            auto* p=m_Player->GetComponent<PhysicsComponent>();auto* b=p?p->GetBody():nullptr;
            if(b){Vec2 v=b->GetLinearVelocity();m_OnGround=std::abs(v.y)<0.5f;float mx=0;
                if(Input::IsKeyDown(KeyCode::A)||Input::IsKeyDown(KeyCode::Left))mx=-1;
                if(Input::IsKeyDown(KeyCode::D)||Input::IsKeyDown(KeyCode::Right))mx=1;
                b->SetLinearVelocity(Vec2(mx*m_PlayerSpeed,v.y));
                if((Input::IsKeyPressed(KeyCode::W)||Input::IsKeyPressed(KeyCode::Up)||Input::IsKeyPressed(KeyCode::Space))&&m_OnGround){b->SetLinearVelocity(Vec2(b->GetLinearVelocity().x,m_JumpForce));if(m_SndJump)Audio::PlayOneShot(*m_Audio,*m_SndJump);}
                auto* s=m_Player->GetComponent<SpriteComponent>();
                if(s){if(!m_OnGround)s->SetTexture(m_Tex["mj"]);else if(std::abs(mx)>0.1f){m_AnimT+=dt;if(m_AnimT>0.2f){m_AnimT=0;m_AnimF=(m_AnimF+1)%4;}s->SetTexture(m_Tex[m_AnimF%2==0?"mw1":"mw2"]);}else s->SetTexture(m_Tex["ms"]);float sx=mx<0?-1.f:(mx>0?1.f:m_Player->GetTransform().GetScale().x);m_Player->GetTransform().SetScale(Vec3(sx,1,1));}}
            if(m_Player->GetTransform().GetPosition().y<-5)m_State=PlayerState::Dead;
        }
        for(auto& e:m_Enemies){if(!e.alive)continue;auto* b=e.obj->GetComponent<PhysicsComponent>();auto* body=b?b->GetBody():nullptr;if(!body)continue;Vec2 v=body->GetLinearVelocity();body->SetLinearVelocity(Vec2(e.dir*e.speed,v.y));if(std::abs(v.x)<0.05f)e.dir*=-1;}
        if(m_Player){Vec3 pp=m_Player->GetTransform().GetPosition();for(auto it=m_Coins.begin();it!=m_Coins.end();){Vec3 cp=(*it)->GetTransform().GetPosition();float d=std::sqrt((pp.x-cp.x)*(pp.x-cp.x)+(pp.y-cp.y)*(pp.y-cp.y));if(d<0.8f){m_Score+=100;m_CoinsCollected++;if(m_SndCoin)Audio::PlayOneShot(*m_Audio,*m_SndCoin);(*it)->SetActive(false);it=m_Coins.erase(it);}else ++it;}}
        if(m_Player&&m_State!=PlayerState::Dead&&m_InvTimer<=0){Vec3 pp=m_Player->GetTransform().GetPosition();Vec2 pv;auto* p=m_Player->GetComponent<PhysicsComponent>();if(p&&p->GetBody())pv=p->GetBody()->GetLinearVelocity();for(auto& e:m_Enemies){if(!e.alive)continue;Vec3 ep=e.obj->GetTransform().GetPosition();float d=std::sqrt((pp.x-ep.x)*(pp.x-ep.x)+(pp.y-ep.y)*(pp.y-ep.y));if(d<0.8f){if(pv.y<-1&&pp.y>ep.y+0.2f){e.alive=false;e.obj->SetActive(false);m_Score+=200;if(m_SndStomp)Audio::PlayOneShot(*m_Audio,*m_SndStomp);if(p&&p->GetBody())p->GetBody()->SetLinearVelocity(Vec2(pv.x,5));}else Die();}}}
        // ── 检测是否到达旗杆（通关） ──
        if(m_Player&&m_State!=PlayerState::Dead&&m_State!=PlayerState::LevelComplete){
            Vec3 pp=m_Player->GetTransform().GetPosition();
            if(pp.x>76.0f){
                m_State=PlayerState::LevelComplete;
                m_LevelCompleteTimer=3.0f;
                std::cout<<"[Mario] 🏁 到达终点！得分:"<<m_Score<<"\n";
                // 停止 BGM 并播放通关音效
                if(m_BgmSource)m_BgmSource->Stop();
                InitLevelCompleteFanfare();
            }
        }
        // ── 更新相机 ──
        if(m_Player){float tx=m_Player->GetTransform().GetPosition().x;float hw=m_ViewWidth*0.5f;tx=std::max(hw,std::min(tx,m_LevelW-hw));m_Camera=OrthographicCamera(tx-hw,tx+hw,0,m_ViewHeight);m_SceneRenderer.SetCamera(&m_Camera);}
        if(m_State==PlayerState::Dead){m_DeathTimer-=dt;if(m_DeathTimer<=0){if(--m_Lives>0)ResetLevel();else{m_Lives=3;m_Score=0;m_CoinsCollected=0;ResetLevel();}}}
        if(m_State==PlayerState::LevelComplete){m_LevelCompleteTimer-=dt;if(m_LevelCompleteTimer<=0)GoToNextLevel();}
        if(m_InvTimer>0)m_InvTimer-=dt;
        if(m_Audio)Audio::UpdateOneShots(*m_Audio);
        if(m_BgmSource&&!m_BgmSource->IsPlaying()&&m_State!=PlayerState::LevelComplete)m_BgmSource->Play(m_BgmBuffer);
        auto* ctx=m_Window->GetContext();if(ctx)ctx->ClearColor(0.1f,0.1f,0.25f,1.0f);
        m_SceneRenderer.Render(m_Scene,m_Tex["ground"]);

        // ── ImGui 渲染 ──
        if (m_UIInitialized && UIManager::Get()) {
            UIManager::Begin();
            m_ConsolePanel.OnImGui();
            m_MemoryPanel.OnImGui();
            UIManager::End();
        }

        // ── 内存帧结束 ──
        MemoryTracker::FrameEnd();

        m_InputManager.OnUpdate();
        if(auto* ctx=m_Window->GetContext())ctx->SwapBuffers();
    }

    // ── 游戏循环退出后：先关闭 UI（必须在窗口销毁前，ImGui 后端需要有效的
    // GLFW/OpenGL 上下文来清理资源） ──
    if (m_UIInitialized) {
        UIManager::Shutdown();
        m_UIInitialized = false;
    }

    // ── 显式清理音频资源，确保 OpenAL 后台线程正常终止 ──
    std::cout << "[Mario] 游戏循环结束，清理音频资源...\n";
    // 1. 停止所有一次性音效
    Audio::StopAllOneShots(*m_Audio);
    // 2. 停止并销毁 BGM 音源
    if(m_BgmSource){
        m_BgmSource->Stop();
        m_Audio->DestroySource(m_BgmSource.get());
        m_BgmSource.reset();
    }
    m_BgmBuffer.reset();
    // 3. 释放 AudioClip 中的 OpenAL 缓冲区（必须在 Shutdown 之前）
    m_SndJump.reset(); m_SndCoin.reset();
    m_SndStomp.reset(); m_SndDie.reset();
    // 4. 主动关闭音频引擎（在 GLFW 窗口销毁前清理 OpenAL 上下文和设备）
    if(m_Audio){
        m_Audio->Shutdown();
        m_Audio.reset();
    }
    std::cout << "[Mario] 音频已关闭，进程退出\n";
}
} // namespace Engine





