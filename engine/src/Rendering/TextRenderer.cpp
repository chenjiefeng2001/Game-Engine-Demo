#include "Engine/Rendering/TextRenderer.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include <glad/gl.h>
#include <iostream>
#include <vector>
#include <algorithm>

// FreeType 引入
#include <ft2build.h>
#include FT_FREETYPE_H

namespace Engine {

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    if (m_Face) FT_Done_Face(m_Face);
    if (m_FT)   FT_Done_FreeType(m_FT);
    if (m_FontTextureID) {
        // 析构时可能无有效 GL 上下文，标记等待自动清理
        m_FontTextureID = 0;
    }
}

bool TextRenderer::Initialize(IRenderContext& context) {
    auto& gl = static_cast<OpenGLContext&>(context).GetGL();    
    m_GLContext = &gl;

    // 默认屏幕尺寸（实际值通过 UpdateScreenSize 设置）
    m_ScreenWidth = 1280;
    m_ScreenHeight = 720;

    // 1. 初始化 FreeType
    if (FT_Init_FreeType(&m_FT)) {
        std::cerr << "[TextRenderer] ERROR: Failed to init FreeType Library\n";
        return false;
    }

    // 2. 加载字体文件
    // Windows: 微软雅黑；Linux/Mac 需相应调整
    const char* fontPaths[] = {
        "C:/Windows/Fonts/msyh.ttc",       // Windows 10/11 微软雅黑
        "C:/Windows/Fonts/msyh.ttf",        // 备选
        "C:/Windows/Fonts/simhei.ttf",      // 黑体
        "C:/Windows/Fonts/arial.ttf",       // Arial (纯英文)
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", // Linux
        "/System/Library/Fonts/PingFang.ttc", // macOS
    };

    bool fontLoaded = false;
    for (auto path : fontPaths) {
        if (FT_New_Face(m_FT, path, 0, &m_Face) == 0) {
            fontLoaded = true;
            std::cout << "[TextRenderer] Loaded font: " << path << "\n";
            break;
        }
    }

    if (!fontLoaded) {
        std::cerr << "[TextRenderer] ERROR: No font found! Text will not render.\n";
        // 继续运行，不崩溃
        return true;
    }

    // 设置基础像素大小（用于高质量位图渲染）
    FT_Set_Pixel_Sizes(m_Face, 0, kBaseFontSize);

    // 3. 创建空的 1024x1024 GL_RED 动态图集纹理
    gl.GenTextures(1, &m_FontTextureID);
    gl.BindTexture(GL_TEXTURE_2D, m_FontTextureID);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_AtlasWidth, m_AtlasHeight, 0,
                   GL_RED, GL_UNSIGNED_BYTE, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.BindTexture(GL_TEXTURE_2D, 0);

    return true;
}

// ─── UTF-8 解码 ───
std::vector<uint32> TextRenderer::DecodeUTF8(const std::string& text) {
    std::vector<uint32> result;
    size_t i = 0;
    while (i < text.length()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        uint32 codePoint = 0;
        int bytes = 0;

        if (c <= 0x7F) {
            codePoint = c;
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            codePoint = c & 0x1F;
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            codePoint = c & 0x0F;
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            codePoint = c & 0x07;
            bytes = 4;
        } else {
            // 无效起始字节，跳过
            i++;
            continue;
        }

        // 解码后续字节
        for (int j = 1; j < bytes; ++j) {
            if (i + j < text.length()) {
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
            }
        }
        result.push_back(codePoint);
        i += bytes;
    }
    return result;
}

// ─── 字形动态加载 ───
void TextRenderer::LoadGlyph(uint32 codePoint) {
    if (m_Glyphs.count(codePoint)) return; // 已加载
    if (!m_Face) return; // 无字体文件

    auto& gl = *static_cast<GladGLContext*>(m_GLContext);

    // 渲染字形到位图
    if (FT_Load_Char(m_Face, codePoint, FT_LOAD_RENDER)) {
        return; // 加载失败（字符不存在）
    }

    FT_Bitmap* bitmap = &m_Face->glyph->bitmap;

    // 检查图集是否需要换行
    if (m_AtlasX + static_cast<int>(bitmap->width) > m_AtlasWidth) {
        m_AtlasX = 0;
        m_AtlasY += m_MaxRowHeight + 1; // 1 像素间距
        m_MaxRowHeight = 0;
    }
    // 如果图集已满（简化处理）
    if (m_AtlasY + static_cast<int>(bitmap->rows) > m_AtlasHeight) {
        std::cerr << "[TextRenderer] Font atlas is full! Increase atlas size.\n";
        return;
    }

    // 上传字形像素到 GPU
    gl.BindTexture(GL_TEXTURE_2D, m_FontTextureID);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1); // FreeType 位图是 1 字节对齐
    if (bitmap->width > 0 && bitmap->rows > 0 && bitmap->buffer) {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0,
                          m_AtlasX, m_AtlasY,
                          bitmap->width, bitmap->rows,
                          GL_RED, GL_UNSIGNED_BYTE,
                          bitmap->buffer);
    }
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4); // 恢复默认
    gl.BindTexture(GL_TEXTURE_2D, 0);

    // 记录字形信息
    GlyphInfo info;
    info.u0 = static_cast<float>(m_AtlasX) / m_AtlasWidth;
    info.v0 = static_cast<float>(m_AtlasY) / m_AtlasHeight;
    info.u1 = static_cast<float>(m_AtlasX + bitmap->width) / m_AtlasWidth;
    info.v1 = static_cast<float>(m_AtlasY + bitmap->rows) / m_AtlasHeight;
    info.width   = static_cast<float>(bitmap->width);
    info.height  = static_cast<float>(bitmap->rows);
    info.bearingX = static_cast<float>(m_Face->glyph->bitmap_left);
    info.bearingY = static_cast<float>(m_Face->glyph->bitmap_top);
    info.advance = static_cast<float>(m_Face->glyph->advance.x >> 6); // 1/64 像素 → 像素

    m_Glyphs[codePoint] = info;

    // 更新图集分配
    m_AtlasX += bitmap->width + 1; // 1 像素间隙
    if (static_cast<int>(bitmap->rows) > m_MaxRowHeight)
        m_MaxRowHeight = bitmap->rows;
}

Vec2 TextRenderer::MeasureText(const std::string& text, float fontSize) {
    std::vector<uint32> chars = DecodeUTF8(text);
    float scale = fontSize / static_cast<float>(kBaseFontSize);
    float width = 0.0f;
    for (uint32 c : chars) {
        LoadGlyph(c);
        auto it = m_Glyphs.find(c);
        if (it != m_Glyphs.end()) {
            width += it->second.advance * scale;
        }
    }
    return Vec2(width, fontSize);
}

void TextRenderer::NormalizeCoords(float& x, float& y, CoordSpace space) const {
    switch (space) {
        case CoordSpace::Normalized:
            break;
        case CoordSpace::Relative:
            x = x * 2.0f - 1.0f;
            y = y * 2.0f - 1.0f;
            break;
        case CoordSpace::Screen:
            x = (x / m_ScreenWidth) * 2.0f - 1.0f;
            y = 1.0f - (y / m_ScreenHeight) * 2.0f;
            break;
    }
}

void TextRenderer::DrawText(IPrimitiveBatch& batch, const std::string& text,
                             float x, float y, const TextConfig& cfg) {
    if (text.empty() || !m_FontTextureID || !m_Face) return;

    std::vector<uint32> chars = DecodeUTF8(text);
    float scale = cfg.fontSize / static_cast<float>(kBaseFontSize);

    // ── 坐标变换 ──
    float nx = x, ny = y;
    // 如果使用归一化坐标或相对坐标，转换到像素
    if (cfg.space == CoordSpace::Normalized) {
        nx = (x + 1.0f) * 0.5f * m_ScreenWidth;
        ny = (1.0f - y) * 0.5f * m_ScreenHeight; // NDC Y 翻转
    } else if (cfg.space == CoordSpace::Relative) {
        nx = x * m_ScreenWidth;
        ny = y * m_ScreenHeight;
    }
    // Screen 空间：直接使用像素坐标

    // ── 对齐处理 ──
    float cursorX = nx;
    float cursorY = ny;
    if (cfg.align == TextAlign::Center || cfg.align == TextAlign::Right) {
        float totalWidth = 0;
        for (uint32 c : chars) {
            LoadGlyph(c);
            auto it = m_Glyphs.find(c);
            if (it != m_Glyphs.end())
                totalWidth += it->second.advance * scale;
        }
        if (cfg.align == TextAlign::Center) cursorX -= totalWidth * 0.5f;
        else if (cfg.align == TextAlign::Right) cursorX -= totalWidth;
    }

    // ── 渲染每个字形 ──
    for (uint32 c : chars) {
        LoadGlyph(c);
        auto it = m_Glyphs.find(c);
        if (it == m_Glyphs.end()) continue;

        const GlyphInfo& info = it->second;

        // 计算像素位置（FreeType 原点在基线）
        float xpos = cursorX + info.bearingX * scale;
        // Y：基准线在(cursorY + baseline)，bearingY 向上为正。
        // 屏幕 Y 向下为正，所以实际像素 y = cursorY + (kBaseFontSize - bearingY) * scale
        float ypos = cursorY + (static_cast<float>(kBaseFontSize) - info.bearingY) * scale;

        float w = info.width * scale;
        float h = info.height * scale;

        // 生成四边形顶点（像素坐标）
        Vec3 corners[4] = {
            Vec3(xpos,       ypos + h, 0), // 左下
            Vec3(xpos,       ypos,     0), // 左上
            Vec3(xpos + w,   ypos,     0), // 右上
            Vec3(xpos + w,   ypos + h, 0), // 右下
        };
        Vec2 uvs[4] = {
            {info.u0, info.v1}, // 左下
            {info.u0, info.v0}, // 左上
            {info.u1, info.v0}, // 右上
            {info.u1, info.v1}, // 右下
        };

        uint32 base = batch.GetVertexCount();
        for (int j = 0; j < 4; ++j) {
            batch.Vertex(corners[j], Vec3(0, 0, 1), uvs[j], cfg.color);
        }
        batch.Index(base);
        batch.Index(base + 1);
        batch.Index(base + 2);
        batch.Index(base);
        batch.Index(base + 2);
        batch.Index(base + 3);

        cursorX += info.advance * scale;
    }

    batch.Commit();
}

void TextRenderer::DrawTextMultiline(IPrimitiveBatch& batch, const std::string& text,
                                      float x, float y, float maxWidth,
                                      const TextConfig& cfg) {
    if (text.empty()) return;

    std::vector<uint32> chars = DecodeUTF8(text);
    float scale = cfg.fontSize / static_cast<float>(kBaseFontSize);
    float lineHeight = cfg.fontSize * cfg.lineSpacing;

    std::vector<uint32> lineChars;
    float lineWidth = 0;
    float cursorY = y;

    for (size_t i = 0; i < chars.size(); ++i) {
        uint32 c = chars[i];

        if (c == '\n') {
            // 渲染当前行
            std::string lineText;
            for (uint32 lc : lineChars) {
                // 简单转换回字符串（仅 ASCII 可工作；中文可存储为 UTF-8）
            }
            // 直接使用字符数组绘制
            if (!lineChars.empty()) {
                for (uint32 cc : lineChars) {
                    uint32 ch = cc;
                    // 构建单字符字符串
                    std::string singleChar;
                    if (ch <= 0x7F) {
                        singleChar += static_cast<char>(ch);
                    } else if (ch <= 0x7FF) {
                        singleChar += static_cast<char>(0xC0 | (ch >> 6));
                        singleChar += static_cast<char>(0x80 | (ch & 0x3F));
                    } else if (ch <= 0xFFFF) {
                        singleChar += static_cast<char>(0xE0 | (ch >> 12));
                        singleChar += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                        singleChar += static_cast<char>(0x80 | (ch & 0x3F));
                    } else {
                        singleChar += static_cast<char>(0xF0 | (ch >> 18));
                        singleChar += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                        singleChar += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                        singleChar += static_cast<char>(0x80 | (ch & 0x3F));
                    }
                    DrawText(batch, singleChar, x + lineWidth, cursorY, cfg);
                }
            }
            lineChars.clear();
            lineWidth = 0;
            cursorY += lineHeight;
            continue;
        }

        lineChars.push_back(c);
        // 测量宽度
        if (maxWidth > 0 && lineWidth > maxWidth && lineChars.size() > 1) {
            // 超宽，换行
            lineChars.pop_back(); // 移除最后一个字符
            // 渲染当前行
            {
                float lw = 0;
                for (uint32 cc : lineChars) {
                    LoadGlyph(cc);
                    auto it = m_Glyphs.find(cc);
                    if (it != m_Glyphs.end()) lw += it->second.advance * scale;
                }
                float cx = x;
                if (cfg.align == TextAlign::Center) cx -= lw * 0.5f;
                else if (cfg.align == TextAlign::Right) cx -= lw;

                // 单字符渲染
                float charOffset = 0;
                for (uint32 cc : lineChars) {
                    std::string singleChar;
                    if (cc <= 0x7F) singleChar += static_cast<char>(cc);
                    else if (cc <= 0x7FF) {
                        singleChar += static_cast<char>(0xC0 | (cc >> 6));
                        singleChar += static_cast<char>(0x80 | (cc & 0x3F));
                    } else if (cc <= 0xFFFF) {
                        singleChar += static_cast<char>(0xE0 | (cc >> 12));
                        singleChar += static_cast<char>(0x80 | ((cc >> 6) & 0x3F));
                        singleChar += static_cast<char>(0x80 | (cc & 0x3F));
                    }
                    DrawText(batch, singleChar, cx + charOffset, cursorY, cfg);
                    auto it2 = m_Glyphs.find(cc);
                    if (it2 != m_Glyphs.end())
                        charOffset += it2->second.advance * scale;
                }
            }
            // 回退最后一个字符
            lineChars.clear();
            lineChars.push_back(c);
            lineWidth = 0;
            cursorY += lineHeight;
        } else {
            LoadGlyph(c);
            auto it2 = m_Glyphs.find(c);
            if (it2 != m_Glyphs.end())
                lineWidth += it2->second.advance * scale;
        }
    }

    // 渲染剩余行
    if (!lineChars.empty()) {
        float cx = x;
        float lw = 0;
        for (uint32 cc : lineChars) {
            LoadGlyph(cc);
            auto it = m_Glyphs.find(cc);
            if (it != m_Glyphs.end()) lw += it->second.advance * scale;
        }
        if (cfg.align == TextAlign::Center) cx -= lw * 0.5f;
        else if (cfg.align == TextAlign::Right) cx -= lw;

        float charOffset = 0;
        for (uint32 cc : lineChars) {
            std::string singleChar;
            if (cc <= 0x7F) singleChar += static_cast<char>(cc);
            else if (cc <= 0x7FF) {
                singleChar += static_cast<char>(0xC0 | (cc >> 6));
                singleChar += static_cast<char>(0x80 | (cc & 0x3F));
            } else if (cc <= 0xFFFF) {
                singleChar += static_cast<char>(0xE0 | (cc >> 12));
                singleChar += static_cast<char>(0x80 | ((cc >> 6) & 0x3F));
                singleChar += static_cast<char>(0x80 | (cc & 0x3F));
            }
            DrawText(batch, singleChar, cx + charOffset, cursorY, cfg);
            auto it = m_Glyphs.find(cc);
            if (it != m_Glyphs.end())
                charOffset += it->second.advance * scale;
        }
    }
}

} // namespace Engine
