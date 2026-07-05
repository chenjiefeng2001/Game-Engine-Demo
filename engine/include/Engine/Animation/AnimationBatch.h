#pragma once

/**
 * @file AnimationBatch.h
 * @brief 动画批量处理 — SoA 数据布局、SIMD 接口预留、热/冷分离
 *
 * ════════════════════════════════════════════
 * 优化目标
 * ════════════════════════════════════════════
 *
 *   PC 瓶颈：内存读取延迟、缓存未命中、CPU 前端停顿
 *   解决策略：
 *     1. SoA (Structure of Arrays) 布局
 *        将 N 个实例的同一字段连续排列，提高空间局部性
 *     2. 热/冷数据分离 (Hot/Cold Splitting)
 *        频繁访问的 pose 数据放在连续热区
 *        不常访问的 clip 名称等放在冷区
 *     3. 16 字节对齐内存分配
 *        为后续 SSE/AVX SIMD 优化预留接口
 *     4. 批处理 vs 单例双模式
 *        AnimationBatch 批量处理
 *        AnimationInstance::Update() 单例处理（移动端/主机）
 *
 * ════════════════════════════════════════════
 * 内存布局
 * ════════════════════════════════════════════
 *
 *   热数据（SoA, 16字节对齐）：
 *     instance 0    1    2    3  ...
 *     transX  [ f0 | f1 | f2 | f3 | ... ]  ← SSE 可以一次加载 4 个
 *     transY  [ f0 | f1 | f2 | f3 | ... ]
 *     transZ  [ f0 | f1 | f2 | f3 | ... ]
 *     rotX    [ f0 | f1 | f2 | f3 | ... ]
 *     ...
 *     localTime [ f0 | f1 | f2 | f3 | ... ]
 *
 *   冷数据（AoS）：
 *     [instance0.clipName, instance0.blendSpec, ...]
 *     [instance1.clipName, instance1.blendSpec, ...]
 *     ...
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   // === PC 批量模式 ===
 *   AnimationBatch batch(32);  // 最多 32 个实例
 *
 *   for (auto& inst : allCharacters) {
 *       batch.AddInstance(&inst);
 *   }
 *
 *   // 单次调用更新所有实例
 *   batch.UpdateAll(dt);
 *
 *   // === 移动端单例模式（不变）===
 *   instance->Update(dt);
 */

#include "Engine/Animation/AnimationInstance.h"
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace Engine {

    // ============================================================
    // SIMD 对齐分配器（16 字节对齐，为 SSE/AVX 准备）
    // ============================================================
    template<typename T>
    class AlignedAllocator {
    public:
        using value_type = T;

        AlignedAllocator() = default;

        template<typename U>
        AlignedAllocator(const AlignedAllocator<U>&) {}

        T* allocate(size_t n) {
            size_t size = n * sizeof(T);
            void* ptr = nullptr;
            // 16 字节对齐分配（SSE 需要 16 字节，AVX 需要 32）
#ifdef _MSC_VER
            ptr = _aligned_malloc(size, 16);
#else
            if (posix_memalign(&ptr, 16, size) != 0) ptr = nullptr;
#endif
            if (!ptr) throw std::bad_alloc();
            return static_cast<T*>(ptr);
        }

        void deallocate(T* ptr, size_t) {
#ifdef _MSC_VER
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }

        template<typename U>
        bool operator==(const AlignedAllocator<U>&) const { return true; }
        template<typename U>
        bool operator!=(const AlignedAllocator<U>&) const { return false; }
    };

    // ============================================================
    // SoA (Structure of Arrays) 姿势缓冲区
    //
    // 将所有实例的同一姿势分量连续排列，使内存访问模式对缓存友好
    // 同时为 SIMD 指令准备好对齐的数据
    // ============================================================
    struct SoAPoseBuffer {
        // 平移 (每个实例 N 根骨骼)
        std::vector<float32, AlignedAllocator<float32>> transX;
        std::vector<float32, AlignedAllocator<float32>> transY;
        std::vector<float32, AlignedAllocator<float32>> transZ;

        // 旋转 (四元数)
        std::vector<float32, AlignedAllocator<float32>> rotX;
        std::vector<float32, AlignedAllocator<float32>> rotY;
        std::vector<float32, AlignedAllocator<float32>> rotZ;
        std::vector<float32, AlignedAllocator<float32>> rotW;

        // 缩放
        std::vector<float32, AlignedAllocator<float32>> scaleX;
        std::vector<float32, AlignedAllocator<float32>> scaleY;
        std::vector<float32, AlignedAllocator<float32>> scaleZ;

        // 矩阵调色板（临时结果）
        std::vector<float32, AlignedAllocator<float32>> matrixData;

        /** 保留的 SIMD 步长（供后续矢量化使用） */
        static constexpr int32 kSIMDStride = 4;  // SSE: 4 floats

        /** 为 batchSize 个实例、boneCount 根骨骼分配内存 */
        void Resize(int32 batchSize, int32 boneCount) {
            int32 total = batchSize * boneCount;
            transX.resize(total, 0.0f);
            transY.resize(total, 0.0f);
            transZ.resize(total, 0.0f);
            rotX.resize(total, 0.0f);
            rotY.resize(total, 0.0f);
            rotZ.resize(total, 0.0f);
            rotW.resize(total, 1.0f);
            scaleX.resize(total, 1.0f);
            scaleY.resize(total, 1.0f);
            scaleZ.resize(total, 1.0f);
            matrixData.resize(total * 16, 0.0f);  // 每个骨骼一个 Mat4
        }

        /** 获取指定实例、指定骨骼的线性索引 */
        int32 Index(int32 instanceIdx, int32 boneIdx) const {
            return instanceIdx * m_StrideBones + boneIdx;
        }

        /** SoA 写入辅助：设置某实例某骨骼的平移 */
        void SetTranslation(int32 inst, int32 bone, const Vec3& t) {
            int32 idx = Index(inst, bone);
            transX[idx] = t.x;
            transY[idx] = t.y;
            transZ[idx] = t.z;
        }

        /** SoA 读取辅助 */
        Vec3 GetTranslation(int32 inst, int32 bone) const {
            int32 idx = Index(inst, bone);
            return Vec3(transX[idx], transY[idx], transZ[idx]);
        }

        void SetRotation(int32 inst, int32 bone, const Quat& r) {
            int32 idx = Index(inst, bone);
            rotX[idx] = r.x; rotY[idx] = r.y;
            rotZ[idx] = r.z; rotW[idx] = r.w;
        }

        Quat GetRotation(int32 inst, int32 bone) const {
            int32 idx = Index(inst, bone);
            return Quat(rotX[idx], rotY[idx], rotZ[idx], rotW[idx]);
        }

        void SetScale(int32 inst, int32 bone, const Vec3& s) {
            int32 idx = Index(inst, bone);
            scaleX[idx] = s.x;
            scaleY[idx] = s.y;
            scaleZ[idx] = s.z;
        }

        Vec3 GetScale(int32 inst, int32 bone) const {
            int32 idx = Index(inst, bone);
            return Vec3(scaleX[idx], scaleY[idx], scaleZ[idx]);
        }

    private:
        /** 骨骼 stride = 对齐到 SIMD 宽度的骨骼数，方便未来向量化 */
        int32 m_StrideBones = 1;  // 初始为 1（无填充）
    };

    // ============================================================
    // 热/冷数据分离的实例运行时数据
    // ============================================================
    struct InstanceHotData {
        float32 localTime;       ///< 当前时间（频繁访问）
        float32 timeScale;       ///< 时间缩放
        float32 blendWeight;     ///< 混合权重
        uint8   state;           ///< 播放状态（小类型打包）
        uint8   loopMode;
        uint8   padding[2];      ///< 填充到 16 字节
    };
    static_assert(sizeof(InstanceHotData) == 16, "InstanceHotData should be 16-byte aligned");

    // ============================================================
    // 动画批量更新器
    // ============================================================
    class AnimationBatch {
    public:
        /**
         * @brief 构造批量更新器
         * @param maxInstances 最大实例数（预分配内存）
         * @param maxBones     最大骨骼数（预分配 SoA 缓冲区）
         */
        explicit AnimationBatch(int32 maxInstances = 128, int32 maxBones = 64);
        ~AnimationBatch();

        // 禁止拷贝
        AnimationBatch(const AnimationBatch&) = delete;
        AnimationBatch& operator=(const AnimationBatch&) = delete;

        // ── 实例管理 ──

        /**
         * @brief 添加实例到批处理
         * @param instance 动画实例指针
         * @return 实例在批处理中的索引（用于移除）
         */
        int32 AddInstance(AnimationInstance* instance);

        /** 从批处理移除实例 */
        void RemoveInstance(int32 index);
        void RemoveInstance(AnimationInstance* instance);

        /** 清空所有实例 */
        void Clear();

        /** 获取当前实例数 */
        int32 GetInstanceCount() const noexcept { return m_InstanceCount; }

        /** 获取最大容量 */
        int32 GetMaxInstances() const noexcept { return m_MaxInstances; }

        // ── 批处理更新 ──

        /**
         * @brief 批量更新所有实例（主入口）
         * @param dt 时间步长
         *
         * 内部流程（SIMD 友好）：
         *   1. 将热数据从实例 gather 到 SoA 缓冲区
         *   2. 批量更新时间线
         *   3. 批量解压姿势到 SoA 缓冲区
         *   4. 批量混合
         *   5. 批量生成全局姿势
         *   6. 批量生成矩阵调色板
         *   7. 将结果 scatter 回各实例
         */
        void UpdateAll(float32 dt);

        // ── 单步操作（可单独调用） ──

        /** 将实例的热数据收集到 SoA 缓冲区 */
        void GatherHotData();

        /** 将 SoA 缓冲区的姿势数据写回各实例 */
        void ScatterResults();

        /** 批量更新时间线 */
        void BatchUpdateTimelines(float32 dt);

        // ── 统计 ──

        struct BatchStats {
            int32   instanceCount   = 0;
            int32   totalBones      = 0;
            size_t  hotDataBytes    = 0;
            size_t  poseBufferBytes = 0;
            float32 lastBatchTimeMs = 0.0f;
        };
        BatchStats GetStats() const noexcept { return m_Stats; }

        // ── SIMD 接口预留 ──

        /** 获取 SIMD 步长（目前为 4，对应 SSE） */
        static constexpr int32 GetSIMDWidth() noexcept { return 4; }

        /** 检查当前平台是否应使用批量模式 */
        static bool IsBatchRecommended() {
            // PC 平台推荐批量模式
            #if defined(ENGINE_PLATFORM_WINDOWS) || defined(ENGINE_PLATFORM_MACOS) || defined(ENGINE_PLATFORM_LINUX)
                return true;
            #else
                return false;
            #endif
        }

    private:
        // ── 热数据（SoA，频繁访问） ──
        InstanceHotData*    m_HotData       = nullptr;  ///< [m_MaxInstances]
        float32*            m_LocalPoseTime = nullptr;  ///< 每个实例增加姿势求值时间

        // ── SoA 姿势缓冲区 ──
        SoAPoseBuffer       m_PoseBuffer;

        // ── 冷数据（AoS，稀疏访问） ──
        AnimationInstance** m_Instances     = nullptr;  ///< [m_MaxInstances]
        int32               m_InstanceCount = 0;
        int32               m_MaxInstances  = 0;
        int32               m_MaxBones      = 0;

        // 统计
        BatchStats          m_Stats;
    };

} // namespace Engine
