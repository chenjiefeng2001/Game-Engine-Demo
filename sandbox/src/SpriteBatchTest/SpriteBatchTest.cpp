#include "SpriteBatchTest.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>

namespace Engine {

    // ─── 将 HSV 转换为 RGB，用于动态颜色 ───
    static void HSVToRGB(float h, float s, float v,
        float& r, float& g, float& b) {
        int i = static_cast<int>(h * 6.0f);
        float f = h * 6.0f - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        }
    }

    // ─────────────────────────────────────────────
    // 构造 & 析构
    // ─────────────────────────────────────────────
    SpriteBatchTest::SpriteBatchTest(IGraphicsFactory& factory)
        : m_Factory(factory)
    {
        m_Window = m_Factory.CreateWindow(800, 600, "Sprite Batch Test");

        // 摄像机覆盖 16×12 的世界坐标范围
        float aspect = 800.0f / 600.0f;
        float viewHeight = 12.0f;
        float viewWidth = viewHeight * aspect;
        m_Camera = std::make_unique<OrthographicCamera>(
            -viewWidth / 2, viewWidth / 2,
            -viewHeight / 2, viewHeight / 2
        );

        // 创建批处理资源
        auto* ctx = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*ctx);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_Factory.CreateTexture("assets/textures/test.png");

        // 初始化 20×20 精灵网格
        m_Sprites.reserve(GRID_SIZE * GRID_SIZE);
        float spacing = 0.5f;
        float offsetX = -(GRID_SIZE - 1) * spacing / 2.0f;
        float offsetY = -(GRID_SIZE - 1) * spacing / 2.0f;

        for (int row = 0; row < GRID_SIZE; row++) {
            for (int col = 0; col < GRID_SIZE; col++) {
                TestSprite s;
                s.baseX = offsetX + col * spacing;
                s.baseY = offsetY + row * spacing;
                s.phase = static_cast<float>(row * GRID_SIZE + col) * 0.3f;
                s.hue = static_cast<float>(row * GRID_SIZE + col)
                    / static_cast<float>(GRID_SIZE * GRID_SIZE);
                m_Sprites.push_back(s);
            }
        }

        std::cout << "=== Sprite Batch Test ===" << std::endl;
        std::cout << "Sprites: " << m_Sprites.size()
            << " (" << GRID_SIZE << "x" << GRID_SIZE << ")" << std::endl;
        std::cout << "Each sprite demonstrates: "
            << "pos(X/Y), scale(pulse), rotation(spin), color(HSV cycle)"
            << std::endl;
    }

    SpriteBatchTest::~SpriteBatchTest() = default;

    // ─────────────────────────────────────────────
    // 更新：每帧修改每个精灵的变换属性
    // ─────────────────────────────────────────────
    void SpriteBatchTest::Update(float dt) {
        m_GlobalTime += dt;

        for (size_t i = 0; i < m_Sprites.size(); i++) {
            auto& s = m_Sprites[i];
            float orbitR = 0.15f;
            float orbitPhase = s.phase + m_GlobalTime * 1.5f;
            float px = s.baseX + std::cos(orbitPhase) * orbitR;
            float py = s.baseY + std::sin(orbitPhase * 0.7f) * orbitR;
            float pulse = 0.5f + 0.2f * std::sin(s.phase + m_GlobalTime * 2.0f);
            float angle = m_GlobalTime * (1.2f + 0.5f * std::sin(s.phase));
            float hue = std::fmod(s.hue + m_GlobalTime * 0.1f, 1.0f);
            float r, g, b;
            HSVToRGB(hue, 0.9f, 1.0f, r, g, b);
        }
    }

    // ─────────────────────────────────────────────
    // 渲染：构建所有精灵 + 单次批量提交
    // ─────────────────────────────────────────────
    void SpriteBatchTest::Render() {
        auto ctx = m_Window->GetContext();
        ctx->ClearColor(0.1f, 0.1f, 0.15f, 1.0f);

        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());

        // ── 开始批次 ──
        m_SpriteBatch->Begin(m_Texture);

        for (size_t i = 0; i < m_Sprites.size(); i++) {
            auto& s = m_Sprites[i];

            float orbitR = 0.15f;
            float orbitPhase = s.phase + m_GlobalTime * 1.5f;
            float px = s.baseX + std::cos(orbitPhase) * orbitR;
            float py = s.baseY + std::sin(orbitPhase * 0.7f) * orbitR;

            float pulse = 0.3f + 0.2f * std::sin(s.phase + m_GlobalTime * 2.0f);
            float angle = m_GlobalTime * (1.2f + 0.5f * std::sin(s.phase));

            float hue = std::fmod(s.hue + m_GlobalTime * 0.1f, 1.0f);
            float r, g, b;
            HSVToRGB(hue, 0.9f, 1.0f, r, g, b);

            SpriteData sprite;
            sprite.transform.x = px;
            sprite.transform.y = py;
            sprite.transform.angle = angle;
            sprite.transform.scaleX = pulse;
            sprite.transform.scaleY = pulse;
            sprite.colorR = r;
            sprite.colorG = g;
            sprite.colorB = b;

            m_SpriteBatch->Draw(sprite);
        }

        // ── 结束批次 → 1 次 Draw Call ──
        m_SpriteBatch->End();
    }

    // ─────────────────────────────────────────────
    // 主循环
    // ─────────────────────────────────────────────
    void SpriteBatchTest::Run() {
        m_LastFrameTime = Time::GetTime();

        while (!m_Window->ShouldClose()) {
            float time = Time::GetTime();
            float dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;

            glfwPollEvents();
            Update(dt);
            Render();

            auto ctx = m_Window->GetContext();
            ctx->SwapBuffers();

            // FPS
            m_FpsAccumulator += dt;
            m_FrameCount++;
            if (m_FpsAccumulator >= 1.0f) {
                std::cout << "[FPS] " << m_FrameCount
                    << " | Sprites: " << m_Sprites.size()
                    << " | Draw Calls: 1"
                    << std::endl;
                m_FpsAccumulator = 0.0f;
                m_FrameCount = 0;
            }
        }
    }

}