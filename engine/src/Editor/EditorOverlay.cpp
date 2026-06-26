#include "Engine/Editor/EditorOverlay.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/Log.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Engine {

    // ============================================================
    // 构造 / 析构
    // ============================================================

    EditorOverlay::EditorOverlay() = default;

    EditorOverlay::~EditorOverlay() {
        Shutdown();
    }

    // ============================================================
    // 初始化
    // ============================================================

    void EditorOverlay::Init(IGraphicsFactory* factory, GladGLContext* gl) {
        if (m_Initialized) return;
        if (!factory || !gl) return;

        m_GL = gl;

        // ── 加载网格着色器 ──
        m_GridShader = factory->CreateShader("assets/shaders/vertex.glsl",
                                              "assets/shaders/fragment.glsl");
        if (!m_GridShader) {
            ENGINE_LOG_ERROR("EditorOverlay", "Failed to load grid shader");
        }

        // ── 加载坐标轴着色器（与网格共用） ──
        m_AxisShader = m_GridShader;

        // ── 创建网格 ──
        if (!CreateGridMesh(factory)) {
            ENGINE_LOG_ERROR("EditorOverlay", "Failed to create grid mesh");
        }

        // ── 创建坐标轴 ──
        if (!CreateAxisMesh(factory)) {
            ENGINE_LOG_ERROR("EditorOverlay", "Failed to create axis mesh");
        }

        m_Initialized = true;
        ENGINE_LOG_INFO("EditorOverlay", "Editor overlay initialized");
    }

    void EditorOverlay::Shutdown() {
        if (!m_Initialized) return;
        m_GridVAO[0].reset();
        m_GridVAO[1].reset();
        m_AxisVAO.reset();
        m_GridShader.reset();
        m_AxisShader.reset();
        m_GL = nullptr;
        m_Initialized = false;
    }

    // ============================================================
    // 网格创建（两个 LOD 级别）
    // ============================================================

    bool EditorOverlay::CreateGridMesh(IGraphicsFactory* factory) {
        // ── LOD0：密集网格（40×40 单元格，41×41 个顶点） ──
        {
            const float halfSize = 10.0f;
            const float cellSize = 0.5f;
            const int segments = static_cast<int>(halfSize * 2.0f / cellSize);
            const int lineCount = (segments + 1) * 2;
            const int vertCount = lineCount * 2;

            std::vector<float> verts;
            verts.reserve(vertCount * 3);

            // X 方向线条（沿 Z 轴排布）
            for (int i = 0; i <= segments; ++i) {
                float z = -halfSize + i * cellSize;
                verts.push_back(-halfSize); verts.push_back(0.0f); verts.push_back(z);
                verts.push_back( halfSize); verts.push_back(0.0f); verts.push_back(z);
            }
            // Z 方向线条（沿 X 轴排布）
            for (int i = 0; i <= segments; ++i) {
                float x = -halfSize + i * cellSize;
                verts.push_back(x); verts.push_back(0.0f); verts.push_back(-halfSize);
                verts.push_back(x); verts.push_back(0.0f); verts.push_back( halfSize);
            }

            auto vb = factory->CreateVertexBuffer(verts.data(), verts.size() * sizeof(float));
            if (!vb) return false;

            m_GridVAO[0] = factory->CreateVertexArray();
            m_GridVAO[0]->AddVertexBuffer(vb);
            m_GridVertexCount[0] = static_cast<uint32>(verts.size() / 3);
        }

        // ── LOD1：粗略网格（20×20 单元格，1.0m 间距） ──
        {
            const float halfSize = 10.0f;
            const float cellSize = 1.0f;
            const int segments = static_cast<int>(halfSize * 2.0f / cellSize);
            const int lineCount = (segments + 1) * 2;
            const int vertCount = lineCount * 2;

            std::vector<float> verts;
            verts.reserve(vertCount * 3);

            for (int i = 0; i <= segments; ++i) {
                float z = -halfSize + i * cellSize;
                verts.push_back(-halfSize); verts.push_back(0.0f); verts.push_back(z);
                verts.push_back( halfSize); verts.push_back(0.0f); verts.push_back(z);
            }
            for (int i = 0; i <= segments; ++i) {
                float x = -halfSize + i * cellSize;
                verts.push_back(x); verts.push_back(0.0f); verts.push_back(-halfSize);
                verts.push_back(x); verts.push_back(0.0f); verts.push_back( halfSize);
            }

            auto vb = factory->CreateVertexBuffer(verts.data(), verts.size() * sizeof(float));
            if (!vb) return false;

            m_GridVAO[1] = factory->CreateVertexArray();
            m_GridVAO[1]->AddVertexBuffer(vb);
            m_GridVertexCount[1] = static_cast<uint32>(verts.size() / 3);
        }

        return true;
    }

    // ============================================================
    // 坐标轴创建（三条彩色箭头线）
    // ============================================================

    bool EditorOverlay::CreateAxisMesh(IGraphicsFactory* factory) {
        float axisVerts[] = {
            0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
        };
        m_AxisVertexCount = 6;

        auto vb = factory->CreateVertexBuffer(axisVerts, sizeof(axisVerts));
        if (!vb) return false;

        m_AxisVAO = factory->CreateVertexArray();
        m_AxisVAO->AddVertexBuffer(vb);
        return true;
    }

    // ============================================================
    // 绘制：无限网格
    // ============================================================

    void EditorOverlay::DrawGrid(GladGLContext* gl,
                                  const float* viewProj,
                                  const float* cameraPos,
                                  const ViewportConfig& config) {
        if (!m_Initialized || !gl || !m_GridShader) return;
        if (!config.ShowGrid) return;

        m_GridShader->Bind();

        // ── 根据相机高度选择 LOD ──
        float camHeight = std::abs(cameraPos[1]) + 0.01f;
        int lod = (camHeight > 15.0f) ? 1 : 0;

        if (!m_GridVAO[lod]) return;
        m_GridVAO[lod]->Bind();

        // ── 构建网格 MVP ──
        glm::mat4 model(1.0f);
        float snapX = std::floor(cameraPos[0] / config.GridCellSize) * config.GridCellSize;
        float snapZ = std::floor(cameraPos[2] / config.GridCellSize) * config.GridCellSize;
        model = glm::translate(model, glm::vec3(snapX, 0.0f, snapZ));

        glm::mat4 mvp = glm::make_mat4(viewProj) * model;

        m_GridShader->SetMat4("u_MVP", glm::value_ptr(mvp));
        m_GridShader->SetMat4("u_Model", glm::value_ptr(model));

        float alpha = std::min(1.0f, 20.0f / camHeight);
        float gridColor[4] = { 0.25f, 0.25f, 0.28f, alpha };
        m_GridShader->SetVec4("u_Color", gridColor);

        // ── 绘制 ──
        gl->LineWidth(1.0f);
        gl->DrawArrays(GL_LINES, 0, m_GridVertexCount[lod]);

        // ── 绘制中心十字轴线（验证网格 VAO 有足够的顶点） ──
        // 网格生成时，前4个顶点恰好是从 (-halfSize,0,0) 到 (+halfSize,0,0) 的
        // X 方向线段，覆盖了中心十字线。直接重用 VAO 的顶点即可。
        if (m_GridVertexCount[lod] >= 4) {
            float axisColor[4] = { 0.4f, 0.4f, 0.45f, alpha };
            m_GridShader->SetVec4("u_Color", axisColor);

            glm::mat4 centerModel(1.0f);
            glm::mat4 centerMVP = glm::make_mat4(viewProj) * centerModel;
            m_GridShader->SetMat4("u_MVP", glm::value_ptr(centerMVP));
            m_GridShader->SetMat4("u_Model", glm::value_ptr(centerModel));

            gl->LineWidth(1.5f);
            gl->DrawArrays(GL_LINES, 0, 4);
            gl->LineWidth(1.0f);
        }
    }

    // ============================================================
    // 绘制：坐标轴指示器（Viewport 左下角小图标）
    // ============================================================

    void EditorOverlay::DrawAxisIndicator(GladGLContext* /*gl*/,
                                           const float* /*view*/,
                                           const ViewportConfig& /*config*/) {
        // ── 坐标轴指示器 ──
        // 在视口左下角绘制一个小的 3D 坐标轴指示器，
        // 显示当前世界坐标系的 X(红) Y(绿) Z(蓝) 方向。
        //
        // 实现思路（未来迭代）：
        //   1. 创建一个正交投影的小视口覆盖在 Viewport 左下角（约 80×80 像素）
        //   2. 禁用深度测试，绘制三条彩色箭头线
        //   3. 箭头方向由世界坐标轴的旋转决定（仅使用 View 矩阵的旋转部分）
        //
        // 当前暂不实现——需要额外的视口矩阵运算，保持在 1.0 版本之后再添加。
    }

} // namespace Engine