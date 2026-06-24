#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Engine {

    ViewportPanel::ViewportPanel(const std::string& name)
        : m_Name(name)
    {
        m_Camera = std::make_unique<EditorCamera>();
    }

    ViewportPanel::~ViewportPanel() = default;

    void ViewportPanel::InitResources(IGraphicsFactory* factory,
                                       const std::string& vertPath,
                                       const std::string& fragPath) {
        m_Factory = factory;
        if (!factory) return;

        m_3DShader = factory->CreateShader(vertPath, fragPath);
        if (!m_3DShader) {
            ENGINE_LOG_ERROR("Viewport", "Failed to load shader: {} {}", vertPath, fragPath);
            return;
        }

        // ── 单位立方体网格 ──
        float vertices[] = {
            -0.5f,-0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f,0.0f,
             0.5f,-0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f,0.0f,
             0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f,1.0f,
             0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f,1.0f,
            -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f,1.0f,
            -0.5f,-0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f,0.0f,
            -0.5f,-0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 0.0f,0.0f,
             0.5f,-0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 1.0f,0.0f,
             0.5f, 0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 1.0f,1.0f,
             0.5f, 0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 1.0f,1.0f,
            -0.5f, 0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 0.0f,1.0f,
            -0.5f,-0.5f,-0.5f, 0.0f, 0.0f,-1.0f, 0.0f,0.0f,
            -0.5f, 0.5f, 0.5f,-1.0f, 0.0f, 0.0f, 0.0f,0.0f,
            -0.5f, 0.5f,-0.5f,-1.0f, 0.0f, 0.0f, 1.0f,0.0f,
            -0.5f,-0.5f,-0.5f,-1.0f, 0.0f, 0.0f, 1.0f,1.0f,
            -0.5f,-0.5f,-0.5f,-1.0f, 0.0f, 0.0f, 1.0f,1.0f,
            -0.5f,-0.5f, 0.5f,-1.0f, 0.0f, 0.0f, 0.0f,1.0f,
            -0.5f, 0.5f, 0.5f,-1.0f, 0.0f, 0.0f, 0.0f,0.0f,
             0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,0.0f,
             0.5f, 0.5f,-0.5f, 1.0f, 0.0f, 0.0f, 1.0f,0.0f,
             0.5f,-0.5f,-0.5f, 1.0f, 0.0f, 0.0f, 1.0f,1.0f,
             0.5f,-0.5f,-0.5f, 1.0f, 0.0f, 0.0f, 1.0f,1.0f,
             0.5f,-0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,1.0f,
             0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,0.0f,
            -0.5f, 0.5f,-0.5f, 0.0f, 1.0f, 0.0f, 0.0f,0.0f,
             0.5f, 0.5f,-0.5f, 0.0f, 1.0f, 0.0f, 1.0f,0.0f,
             0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f,1.0f,
             0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f,1.0f,
            -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f,1.0f,
            -0.5f, 0.5f,-0.5f, 0.0f, 1.0f, 0.0f, 0.0f,0.0f,
            -0.5f,-0.5f,-0.5f, 0.0f,-1.0f, 0.0f, 0.0f,0.0f,
             0.5f,-0.5f,-0.5f, 0.0f,-1.0f, 0.0f, 1.0f,0.0f,
             0.5f,-0.5f, 0.5f, 0.0f,-1.0f, 0.0f, 1.0f,1.0f,
             0.5f,-0.5f, 0.5f, 0.0f,-1.0f, 0.0f, 1.0f,1.0f,
            -0.5f,-0.5f, 0.5f, 0.0f,-1.0f, 0.0f, 0.0f,1.0f,
            -0.5f,-0.5f,-0.5f, 0.0f,-1.0f, 0.0f, 0.0f,0.0f,
        };

        uint32 vertCount = 36;
        uint32 floatsPerVert = 8;
        auto vb = factory->CreateVertexBuffer(vertices, vertCount * floatsPerVert * sizeof(float));
        if (!vb) return;

        uint32 indices[36];
        for (uint32 i = 0; i < 36; ++i) indices[i] = i;
        auto ib = factory->CreateIndexBuffer(indices, 36);
        if (!ib) return;

        m_CubeVAO = factory->CreateVertexArray();
        m_CubeVAO->AddVertexBuffer(vb);
        m_CubeVAO->SetIndexBuffer(ib);

        // ── 地面网格 ──
        float gridSize = 20.0f;
        uint32 segments = 20;
        float halfSz = gridSize / 2.0f;
        float step = gridSize / segments;
        uint32 lineCount = (segments + 1) * 2;
        m_GridVertexCount = lineCount * 2;
        std::vector<float> gridVerts(m_GridVertexCount * 3);

        uint32 idx = 0;
        for (uint32 i = 0; i <= segments; ++i) {
            float pos = -halfSz + i * step;
            gridVerts[idx++] = pos;    gridVerts[idx++] = -0.5f; gridVerts[idx++] = -halfSz;
            gridVerts[idx++] = pos;    gridVerts[idx++] = -0.5f; gridVerts[idx++] =  halfSz;
            gridVerts[idx++] = -halfSz; gridVerts[idx++] = -0.5f; gridVerts[idx++] = pos;
            gridVerts[idx++] =  halfSz; gridVerts[idx++] = -0.5f; gridVerts[idx++] = pos;
        }

        auto gridVB = factory->CreateVertexBuffer(gridVerts.data(),
                                                   m_GridVertexCount * 3 * sizeof(float));
        m_GridVAO = factory->CreateVertexArray();
        m_GridVAO->AddVertexBuffer(gridVB);

        ENGINE_LOG_INFO("Viewport", "{}: 3D resources initialized (cube={}, grid={})",
                        m_Name, vertCount, m_GridVertexCount);
    }

    void ViewportPanel::InitFBO() {
        if (!m_GL) return;
        m_FBO = std::make_unique<OpenGLFramebuffer>(*m_GL);
        uint32 fboW = static_cast<uint32>((m_Width > 0) ? m_Width : 1280);
        uint32 fboH = static_cast<uint32>((m_Height > 0) ? m_Height : 720);
        m_FBO->Resize(fboW, fboH);
    }

    void ViewportPanel::OnUpdate(float32 dt, bool isFocusedViewport) {
        if (!m_Camera || !m_RenderContext) return;

        // ── 焦点控制：只有悬停/聚焦且是唯一焦点视口时才处理输入 ──
        bool canProcessInput = (m_Hovered || m_Focused) && isFocusedViewport;

        m_Camera->SetActive(canProcessInput);
        if (canProcessInput) {
            m_Camera->ProcessInput(dt);
        }
        m_Camera->OnUpdate(dt);

        // ── 渲染 3D 场景到 FBO ──
        Render3DScene();
    }

    void ViewportPanel::OnImGui() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        bool visible = true;
        ImGui::Begin(m_Name.c_str(), &visible,
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        m_Hovered = ImGui::IsWindowHovered();
        m_Focused = ImGui::IsWindowFocused();

        ImVec2 viewPos = ImGui::GetCursorScreenPos();
        m_ViewX = viewPos.x;
        m_ViewY = viewPos.y;

        float32 newW = viewportSize.x;
        float32 newH = viewportSize.y;
        if ((newW != m_Width || newH != m_Height) &&
            newW > 0.0f && newH > 0.0f) {

            m_Width  = newW;
            m_Height = newH;

            if (!m_FBO && m_GL) {
                InitFBO();
            } else if (m_FBO && m_FBO->IsValid()) {
                m_FBO->Resize(static_cast<uint32>(m_Width),
                              static_cast<uint32>(m_Height));
            }

            if (m_Camera) {
                m_Camera->SetViewportSize(m_Width, m_Height);
            }

            if (m_ResizeCallback) {
                m_ResizeCallback(static_cast<int32>(m_Width),
                                 static_cast<int32>(m_Height));
            }
        }

        // ── 显示 FBO 纹理 ──
        if (m_FBO && m_FBO->IsValid() && m_Width > 0 && m_Height > 0) {
            uint32 texID = m_FBO->GetColorTextureID();
            ImGui::Image((ImTextureID)(uint64)texID,
                         ImVec2(m_Width, m_Height),
                         ImVec2(0, 1), ImVec2(1, 0));

            // 悬停提示
            if (m_Hovered) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 pMin = ImGui::GetItemRectMin();
                ImVec2 pMax = ImGui::GetItemRectMax();
                dl->AddRect(pMin, pMax, IM_COL32(255, 200, 50, 180), 0.0f, 0, 2.0f);
            }
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pMin = ImGui::GetCursorScreenPos();
            ImVec2 pMax(pMin.x + m_Width, pMin.y + m_Height);
            if (m_Width > 0 && m_Height > 0) {
                dl->AddRectFilled(pMin, pMax, IM_COL32(30, 30, 35, 255));
                dl->AddText(pMin, IM_COL32(100, 100, 110, 255),
                            ("Viewport: " + m_Name).c_str());
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ViewportPanel::Render3DScene() {
        if (!m_FBO || !m_FBO->IsValid()) {
            if (m_GL) InitFBO();
            if (!m_FBO || !m_FBO->IsValid()) return;
        }

        m_FBO->Bind();

        if (m_GL) {
            m_GL->ClearColor(0.12f, 0.12f, 0.15f, 1.0f);
            m_GL->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            m_GL->Enable(GL_DEPTH_TEST);

            // 根据 ViewMode 设置多边形模式
            if (m_ViewMode == ViewMode::Wireframe) {
                m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            } else {
                m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }

        // ── 构建相机矩阵 ──
        if (!m_Camera) return;
        float aspect = (m_Height > 0) ? m_Width / m_Height : 1.778f;
        glm::mat4 proj;
        if (m_Camera->GetProjectionType() == CameraProjectionType::Orthographic) {
            float zoom = m_Camera->GetDistance() * 0.5f;
            proj = glm::ortho(-zoom * aspect, zoom * aspect, -zoom, zoom, 0.1f, 1000.0f);
        } else {
            proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        }

        glm::mat4 view = glm::lookAt(
            glm::vec3(m_Camera->GetPosition().x, m_Camera->GetPosition().y, m_Camera->GetPosition().z),
            glm::vec3(m_Camera->GetFocusPoint().x, m_Camera->GetFocusPoint().y, m_Camera->GetFocusPoint().z),
            glm::vec3(0.0f, 1.0f, 0.0f));

        if (!m_3DShader || !m_CubeVAO) return;

        m_3DShader->Bind();

        // ── 绘制网格 ──
        if (m_GridVAO) {
            glm::mat4 model(1.0f);
            glm::mat4 mvp = proj * view * model;

            m_3DShader->SetMat4("u_Model", glm::value_ptr(model));
            m_3DShader->SetMat4("u_View", glm::value_ptr(view));
            m_3DShader->SetMat4("u_Projection", glm::value_ptr(proj));
            m_3DShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            m_3DShader->SetMat4("u_NormalMatrix",
                                glm::value_ptr(glm::transpose(glm::inverse(model))));

            Vec3 camPos = m_Camera->GetPosition();
            m_3DShader->SetVec3("u_CameraPos", &camPos.x);

            // 网格用灰色
            if (m_ViewMode == ViewMode::Wireframe) {
                Vec4 brightGrid(0.5f, 0.5f, 0.5f, 1.0f);
                m_3DShader->SetVec4("u_Color", &brightGrid.x);
            } else {
                Vec4 gridCol(0.25f, 0.25f, 0.28f, 1.0f);
                m_3DShader->SetVec4("u_Color", &gridCol.x);
            }

            m_GL->LineWidth(1.0f);
            m_GL->DrawArrays(GL_LINES, 0, m_GridVertexCount);
        }

        // ── 绘制演示物体 ──
        Vec3 camPos = m_Camera->GetPosition();
        m_3DShader->SetVec3("u_CameraPos", &camPos.x);
        m_3DShader->SetMat4("u_View", glm::value_ptr(view));
        m_3DShader->SetMat4("u_Projection", glm::value_ptr(proj));

        // 红色主立方体
        {
            glm::mat4 model(1.0f);
            glm::mat4 mvp = proj * view * model;

            m_3DShader->SetMat4("u_Model", glm::value_ptr(model));
            m_3DShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            m_3DShader->SetMat4("u_NormalMatrix",
                                glm::value_ptr(glm::transpose(glm::inverse(model))));

            Vec4 red(1.0f, 0.2f, 0.2f, 1.0f);
            m_3DShader->SetVec4("u_Color", &red.x);

            m_CubeVAO->Bind();
            m_GL->DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }

        // 蓝色子物体
        {
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(2.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.5f));
            glm::mat4 mvp = proj * view * model;

            m_3DShader->SetMat4("u_Model", glm::value_ptr(model));
            m_3DShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            m_3DShader->SetMat4("u_NormalMatrix",
                                glm::value_ptr(glm::transpose(glm::inverse(model))));

            Vec4 blue(0.2f, 0.3f, 1.0f, 1.0f);
            m_3DShader->SetVec4("u_Color", &blue.x);

            m_CubeVAO->Bind();
            m_GL->DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }

        // 黄色光源标记
        {
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(3.0f, 5.0f, -5.0f));
            model = glm::scale(model, glm::vec3(0.3f));
            glm::mat4 mvp = proj * view * model;

            m_3DShader->SetMat4("u_Model", glm::value_ptr(model));
            m_3DShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            m_3DShader->SetMat4("u_NormalMatrix",
                                glm::value_ptr(glm::transpose(glm::inverse(model))));

            Vec4 yellow(1.0f, 0.9f, 0.1f, 1.0f);
            m_3DShader->SetVec4("u_Color", &yellow.x);

            m_CubeVAO->Bind();
            m_GL->DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }

        // ── 恢复多边形模式 ──
        if (m_ViewMode == ViewMode::Wireframe) {
            m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        m_FBO->Unbind();

        if (m_RenderContext) {
            m_RenderContext->ResetPipelineState();
        }
    }

} // namespace Engine