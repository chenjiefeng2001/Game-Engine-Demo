#include "Engine/Core/RHI/SceneRenderer.h"
#include "Engine/Core/RHI/IRenderable.h"
#include "Engine/Core/Scene/Scene.h"
#include <cmath>

namespace Engine {

    // ──────────────────────────────────────────────
    // 构造 / 初始化
    // ──────────────────────────────────────────────

    SceneRenderer::SceneRenderer(IGraphicsFactory& factory, TextureManager& textureManager)
        : m_Factory(factory)
        , m_TextureManager(textureManager)
    {
    }

    void SceneRenderer::SetRenderContext(IRenderContext& context) {
        m_SpriteBatch = m_Factory.CreateSpriteBatch(context);
    }

    // ──────────────────────────────────────────────
    // 主渲染入口
    // ──────────────────────────────────────────────

    void SceneRenderer::Render(Scene& scene,
                               const std::shared_ptr<Texture>& defaultTexture)
    {
        // 1. 清空上一帧的渲染队列
        m_Queue.Clear();

        // 2. 遍历场景，收集所有对象的渲染命令
        //    通过 IRenderable 接口多态收集，Scene 无需知道具体类型
        scene.CollectRenderCommands(m_Queue);

        // 3. 如果没有 Shader 或 Camera，无法渲染
        if (!m_Shader || !m_Camera)
            return;

        // 4. 绑定 Shader 并设置视图投影矩阵
        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection",
                          m_Camera->GetViewProjectionMatrixPtr());

        // 5. 按纹理分组提交给 ISpriteBatch
        //    默认使用传入的 defaultTexture，若命令中纹理为空则跳过
        const auto& commands = m_Queue.GetCommands();

        // 收集使用的纹理集合（按纹理分组提交）
        std::shared_ptr<Texture> currentTexture;
        bool began = false;

        for (const auto& cmd : commands) {
            // 确定本次使用的纹理
            auto tex = cmd.texture ? cmd.texture : defaultTexture;
            if (!tex)
                continue;

            // 如果纹理切换，结束上一批次
            if (began && tex != currentTexture) {
                m_SpriteBatch->End();
                began = false;
            }

            // 开始新批次
            if (!began) {
                currentTexture = tex;
                m_SpriteBatch->Begin(currentTexture);
                began = true;
            }

            // 将 RenderCommand 转换为 SpriteData 并提交
            SpriteData spriteData;
            ExtractSpriteData(cmd, spriteData);
            m_SpriteBatch->Draw(spriteData);
        }

        if (began) {
            m_SpriteBatch->End();
        }
    }

    // ──────────────────────────────────────────────
    // 内部工具：RenderCommand → SpriteData
    // ──────────────────────────────────────────────

    namespace {

        /**
         * @brief 从 4×4 矩阵提取 2D 变换参数
         *
         * 与 SpriteComponent::ToSpriteData 中的提取逻辑保持一致。
         * 使用 float* 访问矩阵，不直接依赖 glm 数据类型。
         */
        inline void ExtractTransformFromMatrix(const float* m,
                                               float& outX, float& outY,
                                               float& outAngle,
                                               float& outScaleX, float& outScaleY)
        {
            // 矩阵列主序: m[0..3]=col0, m[4..7]=col1, m[8..11]=col2, m[12..15]=col3
            // 位置 = col3 (平移)
            outX = m[12];
            outY = m[13];

            // 缩放 = 各轴基向量长度
            float sx = std::sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
            float sy = std::sqrt(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
            outScaleX = sx;
            outScaleY = sy;

            // 旋转角 = atan2(up.x, right.x) —— 归一化后计算
            float rX = (sx > 0.0001f) ? m[0] / sx : m[0];
            float uX = (sy > 0.0001f) ? m[4] / sy : m[4];
            outAngle = std::atan2(uX, rX);
        }

    } // anonymous namespace

    void SceneRenderer::ExtractSpriteData(const RenderCommand& cmd,
                                          SpriteData& outData) const
    {
        // 从世界矩阵提取位置 / 角度 / 缩放
        ExtractTransformFromMatrix(
            cmd.worldMatrix,
            outData.transform.x, outData.transform.y,
            outData.transform.angle,
            outData.transform.scaleX, outData.transform.scaleY
        );

        // UV
        outData.uvX = cmd.uv[0];
        outData.uvY = cmd.uv[1];
        outData.uvW = cmd.uv[2];
        outData.uvH = cmd.uv[3];

        // 颜色
        outData.colorR = cmd.color[0];
        outData.colorG = cmd.color[1];
        outData.colorB = cmd.color[2];
        outData.colorA = cmd.color[3];
    }

} // namespace Engine
