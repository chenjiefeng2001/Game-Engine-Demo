#pragma once

/**
 * @file SceneSerializer.h
 * @brief 场景序列化器 — 将 ECS 状态保存/加载为 YAML
 *
 * 设计原则：
 *   - 独立类：不污染 Entity.h 核心头文件
 *   - Visitor 模式：通过 EntityManager 遍历组件并序列化
 *   - GUID 先行：场景文件只记录资源 GUID，不记录文件路径
 *
 * 依赖：yaml-cpp（third_party/yaml-cpp/）
 */

#include "Engine/Core/GUID.h"
#include <string>
#include <vector>
#include <cstdint>

// yaml-cpp 前向声明（避免在头文件中引入完整定义）
namespace YAML { class Emitter; class Node; }

namespace Engine {
    class Scene;
    class GameObject;

    // ============================================================
    // SceneSerializer — 场景序列化器
    // ============================================================
    class SceneSerializer {
    public:
        /**
         * @brief 将场景保存到 YAML 文件
         * 
         * @param scene   场景对象
         * @param filePath 输出文件路径（.yaml）
         * @return true    保存成功
         */
        static bool SaveToFile(const Scene& scene, const std::string& filePath);

        /**
         * @brief 从 YAML 文件加载场景
         * 
         * @param scene   场景对象（会被清空并填充）
         * @param filePath 输入文件路径（.yaml）
         * @return true    加载成功
         */
        static bool LoadFromFile(Scene& scene, const std::string& filePath);

        /**
         * @brief 将单个 GameObject 序列化为 YAML 字符串
         * 
         * @param obj GameObject 指针
         * @return std::string YAML 片段
         */
        static std::string SerializeObject(const GameObject* obj);

        /**
         * @brief 从 YAML 节点反序列化 GameObject
         * 
         * @param scene 目标场景
         * @param node  YAML 节点
         * @return GameObject* 新创建的对象指针
         */
        static GameObject* DeserializeObject(Scene& scene, const YAML::Node& node);

    private:
        // ── 内部序列化辅助 ──
        static void SerializeTransform(YAML::Emitter& out, const class Transform& t);
        static bool DeserializeTransform(class Transform& t, const YAML::Node& node);

        static void SerializeMeshComponent(YAML::Emitter& out, const class MeshComponent& mc);
        static bool DeserializeMeshComponent(class MeshComponent& mc, const YAML::Node& node);

        // 场景文件格式版本
        static constexpr int kCurrentVersion = 1;
    };

} // namespace Engine