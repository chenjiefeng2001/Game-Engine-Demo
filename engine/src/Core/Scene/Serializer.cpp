#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include <iostream>
#include <cctype>

namespace Engine {

    // ============================================================
    // TextWriter 实现
    // ============================================================

    void TextWriter::BeginBlock(const std::string& name, const std::string& arg) {
        m_Stream << IndentStr();
        if (arg.empty())
            m_Stream << name << " {\n";
        else
            m_Stream << name << " \"" << arg << "\" {\n";
        m_Indent++;
    }

    void TextWriter::EndBlock() {
        m_Indent--;
        m_Stream << IndentStr() << "}\n";
    }

    void TextWriter::BeginArray(const std::string& name) {
        m_Stream << IndentStr() << name << ": [\n";
        m_Indent++;
    }

    void TextWriter::EndArray() {
        m_Indent--;
        m_Stream << IndentStr() << "]\n";
    }

    void TextWriter::Write(const std::string& key, bool value) {
        m_Stream << IndentStr() << key << ": " << (value ? "true" : "false") << "\n";
    }

    void TextWriter::Write(const std::string& key, int32 value) {
        m_Stream << IndentStr() << key << ": " << value << "\n";
    }

    void TextWriter::Write(const std::string& key, float32 value) {
        m_Stream << IndentStr() << key << ": " << value << "\n";
    }

    void TextWriter::Write(const std::string& key, const std::string& value) {
        m_Stream << IndentStr() << key << ": " << value << "\n";
    }

    void TextWriter::Write(const std::string& key, const char* value) {
        m_Stream << IndentStr() << key << ": " << value << "\n";
    }

    void TextWriter::WriteVec2(const std::string& key, float32 x, float32 y) {
        m_Stream << IndentStr() << key << ": " << x << " " << y << "\n";
    }

    void TextWriter::WriteVec3(const std::string& key, float32 x, float32 y, float32 z) {
        m_Stream << IndentStr() << key << ": " << x << " " << y << " " << z << "\n";
    }

    void TextWriter::WriteVec4(const std::string& key, float32 x, float32 y, float32 z, float32 w) {
        m_Stream << IndentStr() << key << ": " << x << " " << y << " " << z << " " << w << "\n";
    }

    void TextWriter::WriteQuoted(const std::string& key, const std::string& value) {
        m_Stream << IndentStr() << key << ": \"" << value << "\"\n";
    }

    void TextWriter::WriteLine(const std::string& line) {
        m_Stream << IndentStr() << line << "\n";
    }

    // ============================================================
    // TextReader 实现
    // ============================================================

    std::string TextReader::Trim(const std::string& s) const {
        size_t start = 0, end = s.size();
        while (start < end && std::isspace(s[start])) start++;
        while (end > start && std::isspace(s[end-1])) end--;
        return s.substr(start, end - start);
    }

    std::vector<std::string> TextReader::SplitValues(const std::string& str) const {
        std::vector<std::string> result;
        std::string current;
        bool inQuote = false;

        for (char c : str) {
            if (c == '"') {
                inQuote = !inQuote;
                current += c;
            } else if (std::isspace(c) && !inQuote) {
                if (!current.empty()) {
                    result.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) result.push_back(current);
        return result;
    }

    void TextReader::ParseLine(const std::string& line) {
        TokenLine tl;
        tl.raw = line;

        std::string trimmed = Trim(line);

        // 跳过空行和注释
        if (trimmed.empty() || trimmed[0] == '#') return;

        // 检查块结束 }
        if (trimmed == "}") {
            tl.isBlockEnd = true;
            m_Lines.push_back(tl);
            return;
        }

        // 检查数组开始
        if (trimmed.size() > 2 && trimmed.substr(trimmed.size()-2) == ":[") {
            tl.isArrayStart = true;
            tl.key = Trim(trimmed.substr(0, trimmed.size()-2));
            m_Lines.push_back(tl);
            return;
        }

        // 检查数组结束
        if (trimmed == "]") {
            tl.isArrayEnd = true;
            m_Lines.push_back(tl);
            return;
        }

        // 检查 block 开始: "name {" 或 "name \"arg\" {"
        if (trimmed.back() == '{') {
            std::string before = Trim(trimmed.substr(0, trimmed.size()-1));
            if (!before.empty()) {
                // 尝试提取 blockName 和可选的引号参数
                size_t quoteStart = before.find('"');
                if (quoteStart != std::string::npos) {
                    tl.blockName = Trim(before.substr(0, quoteStart));
                    size_t quoteEnd = before.find('"', quoteStart + 1);
                    if (quoteEnd != std::string::npos) {
                        tl.blockArg = before.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    }
                } else {
                    tl.blockName = before;
                }
                m_Lines.push_back(tl);
                return;
            }
        }

        // 检查 key: value(s)
        size_t colon = trimmed.find(':');
        if (colon != std::string::npos) {
            tl.key = Trim(trimmed.substr(0, colon));
            tl.values = SplitValues(Trim(trimmed.substr(colon + 1)));
            m_Lines.push_back(tl);
            return;
        }
    }

    void TextReader::ReadAll() {
        std::string line;
        while (std::getline(m_Stream, line)) {
            ParseLine(line);
        }
    }

    bool TextReader::TryBeginBlock(const std::string& name, std::string& arg) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.blockName == name) {
            arg = tl.blockArg;
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryBeginBlock(const std::string& name) {
        std::string arg;
        return TryBeginBlock(name, arg);
    }

    bool TextReader::TryEndBlock() {
        if (m_Index >= m_Lines.size()) return false;
        if (m_Lines[m_Index].isBlockEnd) {
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryBeginArray(const std::string& name) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.isArrayStart && tl.key == name) {
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryEndArray() {
        if (m_Index >= m_Lines.size()) return false;
        if (m_Lines[m_Index].isArrayEnd) {
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryRead(const std::string& key, bool& value) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && !tl.values.empty()) {
            value = (tl.values[0] == "true");
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryRead(const std::string& key, int32& value) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && !tl.values.empty()) {
            value = std::stoi(tl.values[0]);
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryRead(const std::string& key, float32& value) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && !tl.values.empty()) {
            value = std::stof(tl.values[0]);
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryRead(const std::string& key, std::string& value) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && !tl.values.empty()) {
            value = tl.values[0];
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryReadQuoted(const std::string& key, std::string& value) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && !tl.values.empty()) {
            std::string raw = tl.values[0];
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                value = raw.substr(1, raw.size() - 2);
                m_Index++;
                return true;
            }
        }
        return false;
    }

    bool TextReader::TryReadVec2(const std::string& key, float32& x, float32& y) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && tl.values.size() >= 2) {
            x = std::stof(tl.values[0]);
            y = std::stof(tl.values[1]);
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryReadVec3(const std::string& key, float32& x, float32& y, float32& z) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && tl.values.size() >= 3) {
            x = std::stof(tl.values[0]);
            y = std::stof(tl.values[1]);
            z = std::stof(tl.values[2]);
            m_Index++;
            return true;
        }
        return false;
    }

    bool TextReader::TryReadVec4(const std::string& key, float32& x, float32& y, float32& z, float32& w) {
        if (m_Index >= m_Lines.size()) return false;
        auto& tl = m_Lines[m_Index];
        if (tl.key == key && tl.values.size() >= 4) {
            x = std::stof(tl.values[0]);
            y = std::stof(tl.values[1]);
            z = std::stof(tl.values[2]);
            w = std::stof(tl.values[3]);
            m_Index++;
            return true;
        }
        return false;
    }

    void TextReader::SkipLine() {
        if (m_Index < m_Lines.size()) m_Index++;
    }

    // ============================================================
    // SceneSerializer 实现
    // ============================================================

    bool SceneSerializer::SaveToFile(const Scene& scene, const std::string& filePath) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[Serializer] Failed to write: " << filePath << std::endl;
            return false;
        }

        TextWriter writer(file);
        bool result = Save(writer, scene);
        file.close();

        if (result) {
            std::cout << "[Serializer] Scene saved: " << filePath << std::endl;
        }
        return result;
    }

    bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[Serializer] Failed to read: " << filePath << std::endl;
            return false;
        }

        TextReader reader(file);
        bool result = Load(reader, scene);
        file.close();

        if (result) {
            std::cout << "[Serializer] Scene loaded: " << filePath << std::endl;
        }
        return result;
    }

    bool SceneSerializer::Save(TextWriter& writer, const Scene& scene) {
        writer.BeginBlock("scene", scene.GetName());

        // 场景属性
        const auto& props = scene.GetProperties();
        writer.WriteVec4("ambientColor", props.ambientColor.x, props.ambientColor.y,
                         props.ambientColor.z, props.ambientColor.w);
        writer.WriteVec2("gravity", props.gravity.x, props.gravity.y);
        writer.Write("fogDensity", props.fogDensity);
        writer.WriteVec4("fogColor", props.fogColor.x, props.fogColor.y,
                         props.fogColor.z, props.fogColor.w);
        writer.Write("enableFog", props.enableFog ? 1 : 0);
        writer.Write("renderingOrder", static_cast<int32>(props.renderingOrder));

        // 根对象
        writer.BeginArray("");
        for (const auto& obj : scene.GetObjects()) {
            SaveGameObject(writer, *obj);
        }
        writer.EndArray();

        writer.EndBlock();  // scene
        return true;
    }

    bool SceneSerializer::Load(TextReader& reader, Scene& scene) {
        std::string sceneName;
        if (!reader.TryBeginBlock("scene", sceneName)) {
            std::cerr << "[Serializer] Expected 'scene' block" << std::endl;
            return false;
        }
        if (!sceneName.empty()) scene.SetName(sceneName);

        // 场景属性
        float32 x, y, z, w;
        if (reader.TryReadVec4("ambientColor", x, y, z, w))
            scene.SetAmbientColor(Vec4(x, y, z, w));
        if (reader.TryReadVec2("gravity", x, y))
            scene.SetGravity(Vec2(x, y));
        float32 f;
        if (reader.TryRead("fogDensity", f)) scene.SetFogDensity(f);
        if (reader.TryReadVec4("fogColor", x, y, z, w))
            scene.SetFogColor(Vec4(x, y, z, w));
        bool b;
        if (reader.TryRead("enableFog", b)) scene.SetFogEnabled(b);
        int32 i;
        if (reader.TryRead("renderingOrder", i))
            scene.GetProperties().renderingOrder = static_cast<uint32>(i);

        // 根对象
        reader.TryBeginArray("");
        while (!reader.IsEOF()) {
            auto obj = std::make_shared<GameObject>();
            if (!LoadGameObject(reader, *obj)) break;
            scene.AddObject(std::move(obj));
        }

        reader.TryEndBlock();  // scene
        return true;
    }

    // ============================================================
    // 内部：保存
    // ============================================================

    void SceneSerializer::SaveGameObject(TextWriter& writer, const GameObject& obj) {
        writer.BeginBlock("gameobject", obj.GetName());

        writer.Write("active", obj.IsActive() ? 1 : 0);

        // 依次保存各组件
        SaveTransform(writer, obj.GetTransform());

        if (auto* sprite = obj.GetComponent<SpriteComponent>())
            SaveSprite(writer, *sprite);

        if (auto* physics = obj.GetComponent<PhysicsComponent>())
            SavePhysics(writer, *physics);

        // 保存子对象
        if (!obj.GetChildren().empty()) {
            writer.BeginArray("children");
            for (const auto& child : obj.GetChildren()) {
                SaveGameObject(writer, *child);
            }
            writer.EndArray();
        }

        writer.EndBlock();  // gameobject
    }

    void SceneSerializer::SaveTransform(TextWriter& writer, const TransformComponent& comp) {
        writer.BeginBlock("TransformComponent");
        const Vec3& p = comp.GetPosition();
        const Vec3& r = comp.GetRotation();
        const Vec3& s = comp.GetScale();
        writer.WriteVec3("position", p.x, p.y, p.z);
        writer.WriteVec3("rotation", r.x, r.y, r.z);
        writer.WriteVec3("scale", s.x, s.y, s.z);
        writer.EndBlock();
    }

    void SceneSerializer::SaveSprite(TextWriter& writer, const SpriteComponent& comp) {
        writer.BeginBlock("SpriteComponent");
        // 纹理路径
        if (comp.HasTexture() && comp.GetTexture()) {
            writer.WriteQuoted("texture", comp.GetTexture()->GetPath());
        }
        const Vec4& c = comp.GetColor();
        writer.WriteVec4("color", c.x, c.y, c.z, c.w);
        writer.WriteVec4("uv", comp.GetUVX(), comp.GetUVY(), comp.GetUVW(), comp.GetUVH());
        writer.WriteVec2("tiling", comp.GetTiling().x, comp.GetTiling().y);
        writer.WriteVec2("offset", comp.GetOffset().x, comp.GetOffset().y);
        writer.Write("sortingLayer", comp.GetSortingLayer());
        writer.Write("orderInLayer", comp.GetOrderInLayer());
        writer.Write("visible", comp.IsVisible() ? 1 : 0);
        writer.EndBlock();
    }

    void SceneSerializer::SavePhysics(TextWriter& writer, const PhysicsComponent& comp) {
        // PhysicsComponent 的序列化需要 BodyDef 信息
        // 当前存为占位，后续扩展
        writer.BeginBlock("PhysicsComponent");
        writer.Write("hasBody", comp.HasBody() ? 1 : 0);
        writer.EndBlock();
    }

    // ============================================================
    // 内部：加载
    // ============================================================

    bool SceneSerializer::LoadGameObject(TextReader& reader, GameObject& obj) {
        std::string objName;
        if (!reader.TryBeginBlock("gameobject", objName))
            return false;
        if (!objName.empty()) obj.SetName(objName);

        bool active = true;
        reader.TryRead("active", active);
        obj.SetActive(active);

        // 依次加载组件
        while (!reader.IsEOF()) {
            if (reader.TryEndBlock()) return true;

            // 检查 children 数组
            if (reader.TryBeginArray("children")) {
                while (!reader.IsEOF()) {
                    auto child = std::make_shared<GameObject>();
                    if (!LoadGameObject(reader, *child)) {
                        break;
                    }
                    obj.AddChild(std::move(child));
                }
                reader.TryEndArray();
                continue;
            }

            // 尝试加载已知组件类型
            bool loaded = LoadComponentForObject(reader, obj);
            if (!loaded) {
                // 未知内容，跳过
                reader.SkipLine();
            }
        }
        return true;
    }

    bool SceneSerializer::LoadTransform(TextReader& reader, TransformComponent& comp) {
        float32 x, y, z;
        if (reader.TryReadVec3("position", x, y, z))
            comp.SetPosition(x, y, z);
        if (reader.TryReadVec3("rotation", x, y, z))
            comp.SetRotation(x, y, z);
        if (reader.TryReadVec3("scale", x, y, z))
            comp.SetScale(Vec3(x, y, z));
        return true;
    }

    bool SceneSerializer::LoadSprite(TextReader& reader, SpriteComponent& comp) {
        std::string texPath;
        if (reader.TryReadQuoted("texture", texPath) && !texPath.empty()) {
            // 通过 ResourceManager 加载纹理
            if (auto* rm = ResourceManager::Get()) {
                auto tex = rm->LoadTexture(texPath);
                if (tex) comp.SetTexture(tex);
            }
        }
        float32 a, b, c, d;
        if (reader.TryReadVec4("color", a, b, c, d))
            comp.SetColor(a, b, c, d);
        if (reader.TryReadVec4("uv", a, b, c, d))
            comp.SetUV(a, b, c, d);
        if (reader.TryReadVec2("tiling", a, b))
            comp.SetTiling(Vec2(a, b));
        if (reader.TryReadVec2("offset", a, b))
            comp.SetOffset(Vec2(a, b));
        int32 i;
        if (reader.TryRead("sortingLayer", i)) comp.SetSortingLayer(i);
        if (reader.TryRead("orderInLayer", i)) comp.SetOrderInLayer(i);
        bool visible = true;
        reader.TryRead("visible", visible);
        comp.SetVisible(visible);
        return true;
    }

    bool SceneSerializer::LoadPhysics(TextReader& reader, PhysicsComponent& comp) {
        bool hasBody = false;
        reader.TryRead("hasBody", hasBody);
        (void)comp; // Physics body 重建需要外部物理世界，此处仅标记
        return true;
    }

    bool SceneSerializer::LoadComponentForObject(TextReader& reader, GameObject& obj) {
        // 使用 TextReader 的拷贝来 peek
        if (reader.TryBeginBlock("TransformComponent")) {
            auto& comp = obj.GetTransform();
            LoadTransform(reader, comp);
            reader.TryEndBlock();
            return true;
        }
        if (reader.TryBeginBlock("SpriteComponent")) {
            auto* comp = obj.GetComponent<SpriteComponent>();
            if (!comp) comp = obj.AddComponent<SpriteComponent>();
            if (comp) {
                LoadSprite(reader, *comp);
                reader.TryEndBlock();
            }
            return true;
        }
        if (reader.TryBeginBlock("PhysicsComponent")) {
            auto* comp = obj.GetComponent<PhysicsComponent>();
            if (!comp) comp = obj.AddComponent<PhysicsComponent>();
            if (comp) {
                LoadPhysics(reader, *comp);
                reader.TryEndBlock();
            }
            return true;
        }
        return false;
    }

} // namespace Engine