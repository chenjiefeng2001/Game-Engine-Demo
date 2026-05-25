#pragma once

/**
 * @file Serializer.h
 * @brief 场景序列化/反序列化 — 将 Scene 导出为元数据文本，支持重建
 *
 * 格式说明（纯文本，可读可编辑）：
 * @code
 *   scene "MainScene"
 *   ambientColor: 0.02 0.02 0.03 1.0
 *   gravity: 0 -9.81
 *   renderingOrder: 0
 *
 *   gameobject "Player" {
 *       active: true
 *       TransformComponent {
 *           position: 0 0 0
 *           rotation: 0 0 0
 *           scale: 1 1 1
 *       }
 *       SpriteComponent {
 *           texture: "assets/textures/player.png"
 *           color: 1 1 1 1
 *           visible: true
 *       }
 *       gameobject "Camera" {
 *           TransformComponent {}
 *       }
 *   }
 * @endcode
 *
 * 使用方法：
 * @code
 *   Scene scene("Main");
 *   // ... 构建场景 ...
 *   SceneSerializer::SaveToFile(scene, "assets/scenes/main.scene");
 *
 *   Scene loaded;
 *   SceneSerializer::LoadFromFile(loaded, "assets/scenes/main.scene");
 * @endcode
 */

#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Types.h"

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <functional>

namespace Engine {

    // ============================================================
    // 文本序列化流 — 写
    // ============================================================
    class TextWriter {
    public:
        explicit TextWriter(std::ostream& stream) : m_Stream(stream) {}

        // ── 结构 ──
        void BeginBlock(const std::string& name, const std::string& arg = "");
        void EndBlock();
        void BeginArray(const std::string& name);
        void EndArray();

        // ── 简单值 ──
        void Write(const std::string& key, bool value);
        void Write(const std::string& key, int32 value);
        void Write(const std::string& key, float32 value);
        void Write(const std::string& key, const std::string& value);
        void Write(const std::string& key, const char* value);
        void WriteVec2(const std::string& key, float32 x, float32 y);
        void WriteVec3(const std::string& key, float32 x, float32 y, float32 z);
        void WriteVec4(const std::string& key, float32 x, float32 y, float32 z, float32 w);
        void WriteQuoted(const std::string& key, const std::string& value);
        void WriteLine(const std::string& line);

        // ── 缩进 ──
        void Indent() { m_Indent++; }
        void Unindent() { if (m_Indent > 0) m_Indent--; }

    private:
        std::ostream& m_Stream;
        int m_Indent = 0;
        std::string IndentStr() const { return std::string(m_Indent * 4, ' '); }
    };

    // ============================================================
    // 文本反序列化流 — 读
    // ============================================================
    class TextReader {
    public:
        explicit TextReader(std::istream& stream) : m_Stream(stream) { ReadAll(); }

        // ── 结构 ──
        /** 尝试匹配块开始，如 "scene \"MainScene\" {" → 成功则消耗 */
        bool TryBeginBlock(const std::string& name, std::string& arg);
        bool TryBeginBlock(const std::string& name);
        /** 匹配块结束 } */
        bool TryEndBlock();
        /** 尝试匹配数组开始 */
        bool TryBeginArray(const std::string& name);
        /** 尝试匹配数组结束 ] */
        bool TryEndArray();

        // ── 读取值 ──
        bool TryRead(const std::string& key, bool& value);
        bool TryRead(const std::string& key, int32& value);
        bool TryRead(const std::string& key, float32& value);
        bool TryRead(const std::string& key, std::string& value);  // 无引号
        bool TryReadQuoted(const std::string& key, std::string& value);
        bool TryReadVec2(const std::string& key, float32& x, float32& y);
        bool TryReadVec3(const std::string& key, float32& x, float32& y, float32& z);
        bool TryReadVec4(const std::string& key, float32& x, float32& y, float32& z, float32& w);

        /** 跳过当前行（用于未知字段或注释） */
        void SkipLine();
        /** 检查是否已到达数据末尾 */
        bool IsEOF() const { return m_Index >= m_Lines.size(); }

    private:
        void ReadAll();

        struct TokenLine {
            std::string raw;
            std::string key;   // 冒号前的部分
            std::vector<std::string> values;  // 冒号后的值列表（按空格分割）
            std::string blockName;  // "blockName {" 这样的行
            std::string blockArg;   // block 后面的引号参数
            bool isBlockEnd = false;
            bool isArrayStart = false;
            bool isArrayEnd = false;
        };

        std::istream& m_Stream;
        std::vector<TokenLine> m_Lines;
        size_t m_Index = 0;

        void ParseLine(const std::string& line);
        std::vector<std::string> SplitValues(const std::string& str) const;
        std::string Trim(const std::string& s) const;
    };

    // ============================================================
    // SceneSerializer
    // ============================================================
    class SceneSerializer {
    public:
        /** 将 Scene 保存到文件 */
        static bool SaveToFile(const Scene& scene, const std::string& filePath);

        /** 从文件加载 Scene */
        static bool LoadFromFile(Scene& scene, const std::string& filePath);

        /** 将 Scene 保存到输出流 */
        static bool Save(TextWriter& writer, const Scene& scene);

        /** 从输入流加载 Scene */
        static bool Load(TextReader& reader, Scene& scene);

    private:
        // ── 内部：保存 ──
        static void SaveGameObject(TextWriter& writer, const GameObject& obj);
        static void SaveTransform(TextWriter& writer, const TransformComponent& comp);
        static void SaveSprite(TextWriter& writer, const SpriteComponent& comp);
        static void SavePhysics(TextWriter& writer, const PhysicsComponent& comp);
        static void SaveComponent(TextWriter& writer, const Component& comp);

        // ── 内部：加载 ──
        static bool LoadGameObject(TextReader& reader, GameObject& obj);
        static bool LoadTransform(TextReader& reader, TransformComponent& comp);
        static bool LoadSprite(TextReader& reader, SpriteComponent& comp);
        static bool LoadPhysics(TextReader& reader, PhysicsComponent& comp);
        static bool LoadComponentForObject(TextReader& reader, GameObject& obj);
    };

} // namespace Engine