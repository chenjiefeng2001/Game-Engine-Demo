#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

    /**
     * @brief 变换组件 — 描述游戏对象的位置、旋转、缩放
     *
     * 支持：
     *   - 局部 / 世界变换矩阵
     *   - 父级层级变换链
     *   - 朝向向量 (Forward / Right / Up)
     */
    class TransformComponent {
    public:
        TransformComponent();
        explicit TransformComponent(const glm::vec3& position);
        TransformComponent(const glm::vec3& position,
                           const glm::vec3& rotation,
                           const glm::vec3& scale);

        // ── 访问器 ──
        const glm::vec3& GetPosition() const noexcept { return m_Position; }
        const glm::vec3& GetRotation() const noexcept { return m_Rotation; }
        const glm::vec3& GetScale()     const noexcept { return m_Scale; }

        void SetPosition(const glm::vec3& pos)     { m_Position = pos; m_Dirty = true; }
        void SetRotation(const glm::vec3& rot)     { m_Rotation = rot; m_Dirty = true; }
        void SetScale(const glm::vec3& s)          { m_Scale = s;    m_Dirty = true; }

        // ── 便捷设置 ──
        void SetPosition(float x, float y, float z) { SetPosition({ x, y, z }); }
        void SetRotation(float pitch, float yaw, float roll) { SetRotation({ pitch, yaw, roll }); }
        void SetScale(float uniform)                { SetScale({ uniform, uniform, uniform }); }

        // ── 相对变换 ──
        void Translate(const glm::vec3& delta)     { m_Position += delta; m_Dirty = true; }
        void Rotate(const glm::vec3& eulerDelta)   { m_Rotation += eulerDelta; m_Dirty = true; }
        void ScaleBy(const glm::vec3& factor)      { m_Scale *= factor; m_Dirty = true; }

        // ── 矩阵 ──
        /** 局部变换矩阵 (T * R * S) */
        const glm::mat4& GetLocalMatrix();
        /** 世界变换矩阵 — 如果无父级则等于局部矩阵 */
        const glm::mat4& GetWorldMatrix();

        // ── 朝向向量 (从世界矩阵提取，调用前确保矩阵已更新) ──
        glm::vec3 GetForward() const noexcept { return glm::normalize(glm::vec3(m_WorldMatrix[2])); }
        glm::vec3 GetRight()   const noexcept { return glm::normalize(glm::vec3(m_WorldMatrix[0])); }
        glm::vec3 GetUp()      const noexcept { return glm::normalize(glm::vec3(m_WorldMatrix[1])); }

        // ── 层级 ──
        void SetParent(TransformComponent* parent) { m_Parent = parent; m_Dirty = true; }
        TransformComponent* GetParent() const noexcept { return m_Parent; }
        bool HasParent() const noexcept { return m_Parent != nullptr; }

    private:
        friend class GameObject;  

        void RecalculateLocalMatrix();
        void RecalculateWorldMatrix();

        // ── 数据 ──
        glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };  
        glm::vec3 m_Scale    = { 1.0f, 1.0f, 1.0f };

        glm::mat4 m_LocalMatrix  = glm::mat4(1.0f);
        glm::mat4 m_WorldMatrix  = glm::mat4(1.0f);

        bool m_Dirty = true;

        TransformComponent* m_Parent = nullptr;
    };

} // namespace Engine
