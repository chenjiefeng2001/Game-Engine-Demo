#pragma once
/**
 * @file TextRenderer.h
 * @brief 文本渲染器 — 基于 FreeType 的动态字形渲染
 *
 * 支持：
 *   - 所有 Unicode 字符（中文、日文、特殊符号等）
 *   - 动态字形装载（纹理图集动态扩展）
 *   - 像素坐标排版（配合正交投影矩阵使用）
 *   - 多行文字、对齐、字号缩放
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// FreeType 前置声明（避免头文件污染）
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_*    FT_Face;

namespace Engine {

    class IPrimitiveBatch;
    class Shader;
    class IRenderContext;

    enum class TextAlign : uint8 {
        Left   = 0,
        Center = 1,
        Right  = 2,
    };

    enum class CoordSpace : uint8 {
        Screen = 0,     // 像素坐标 (0,0)=左上
        Normalized = 1, // 归一化坐标 (-1,-1=左下, 1,1=右上)
        Relative = 2,   // 相对坐标 (0-1 范围)
    };

    struct TextConfig {
        float   fontSize    = 24.0f;
        Vec4    color       = {1,1,1,1};
        TextAlign align     = TextAlign::Left;
        CoordSpace space    = CoordSpace::Screen;
        float   lineSpacing = 1.5f;
    };

    /// 单个字形的图集信息
    struct GlyphInfo {
        float u0, v0, u1, v1; // 纹理坐标（归一化）
        float width, height;   // 像素尺寸
        float bearingX, bearingY; // 偏移
        float advance;         // 步进（像素）
    };

    class TextRenderer {
    public:
        TextRenderer();
        ~TextRenderer();

        TextRenderer(const TextRenderer&) = delete;
        TextRenderer& operator=(const TextRenderer&) = delete;

        /** 初始化 — 加载 FreeType 并创建空图集纹理 */
        bool Initialize(IRenderContext& context);

        /** 设置着色器 */
        void SetShader(Shader* shader) { m_Shader = shader; }

        /** 更新屏幕尺寸（窗口 resize 时调用） */
        void UpdateScreenSize(float w, float h) { m_ScreenWidth = w; m_ScreenHeight = h; }

        /** 渲染一行文本 */
        void DrawText(IPrimitiveBatch& batch, const std::string& text,
                      float x, float y, const TextConfig& cfg = TextConfig{});

        /** 渲染多行文本 */
        void DrawTextMultiline(IPrimitiveBatch& batch, const std::string& text,
                               float x, float y, float maxWidth,
                               const TextConfig& cfg = TextConfig{});

        /** 测量文本宽度 */
        Vec2 MeasureText(const std::string& text, float fontSize = 24.0f);

        /** 获取图集纹理 ID */
        uint32 GetFontTextureID() const { return m_FontTextureID; }

    private:
        /** 将 UTF-8 字符串解码为 Unicode 码位 */
        std::vector<uint32> DecodeUTF8(const std::string& text);

        /** 动态加载单个字形的位图到图集 */
        void LoadGlyph(uint32 codePoint);

        /** 将像素坐标归一化到 NDC（用于 CoordSpace::Normalized / Relative） */
        void NormalizeCoords(float& x, float& y, CoordSpace space) const;

        // OpenGL 上下文指针
        void* m_GLContext = nullptr;

        // OpenGL 图集纹理
        uint32 m_FontTextureID = 0;
        Shader* m_Shader = nullptr;

        // 屏幕尺寸（像素）
        float m_ScreenWidth  = 1280;
        float m_ScreenHeight = 720;

        // FreeType 状态
        FT_Library m_FT = nullptr;
        FT_Face    m_Face = nullptr;

        // 图集参数
        std::unordered_map<uint32, GlyphInfo> m_Glyphs;
        int m_AtlasWidth  = 1024;
        int m_AtlasHeight = 1024;
        int m_AtlasX = 0;
        int m_AtlasY = 0;
        int m_MaxRowHeight = 0;

        // 加载基准字号
        static constexpr int kBaseFontSize = 48;
    };

} // namespace Engine
