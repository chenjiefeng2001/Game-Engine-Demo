#pragma once

/**
 * @file GPUParticle.h
 * @brief GPU 物理引擎 MVP — 基于 Compute Shader 的粒子/球体离散元模拟
 *
 * 设计原则：
 *   - 纯 RHI 抽象层：GPUPhysicsEngine 不包含任何 CPU 物理模拟代码
 *   - 影子内存：Stub 模式下使用 std::vector<uint8_t> 后备存储，
 *     使 Upload→Readback 管线在无 GPU 环境下也可验证
 *   - CPU 端粒子物理验证由外部测试独立实现（见 sandbox CPUSimulator）
 */

#include "Engine/Types.h"
#include "Engine/Core/Physics/FixedTimestepAccumulator.h"
#include <cstdint>
#include <vector>
#include <string>
#include <cstddef> // offsetof
#include <memory>

namespace Engine {

namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHIBuffer;
    class IRHIPipelineState;
}

// ═══════════════════════════════════════════════════════════
// 粒子数据结构（CPU 与 GPU 共享，std430 对齐）
// ═══════════════════════════════════════════════════════════
#pragma pack(push, 4)
struct GPUParticleData {
    float position[3];   // offset 0,  12 bytes
    float radius;        // offset 12, 4 bytes
    float velocity[3];   // offset 16, 12 bytes
    float mass;          // offset 28, 4 bytes
    float color[4];      // offset 32, 16 bytes
    float padding[4];    // offset 48, 16 bytes  → total 64 bytes
};
#pragma pack(pop)

// 编译期校验 std430 布局（与 GLSL std430 对齐规则一致）
static_assert(offsetof(GPUParticleData, position) == 0,  "pos must be at offset 0");
static_assert(offsetof(GPUParticleData, radius)   == 12, "rad must be at offset 12");
static_assert(offsetof(GPUParticleData, velocity) == 16, "vel must be at offset 16");
static_assert(offsetof(GPUParticleData, mass)     == 28, "mass must be at offset 28");
static_assert(offsetof(GPUParticleData, color)    == 32, "color must be at offset 32");
static_assert(sizeof(GPUParticleData) == 64,             "GPUParticleData must be exactly 64 bytes");

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎配置
// ═══════════════════════════════════════════════════════════

/// 碰撞 broad-phase 后端（Phase 4：算法替换，不改求解器语义）
enum class GPUCollisionBackend {
    BruteForce,   ///< O(N²) 全量遍历 —— 参考路径 / Ring7 diff 基准
    SpatialHash,  ///< Uniform Grid + 链表桶 + 27-Cell 邻域 —— 可扩展路径
};

struct GPUPhysicsConfig {
    uint32_t particleCount  = 65536;
    float    gravity[3]     = {0.0f, -9.8f, 0.0f};
    float    restitution    = 0.8f;
    float    stiffness      = 1000.0f;
    float    damping        = 0.02f;
    float    boxMin[3]      = {-50.0f, 0.0f, -50.0f};
    float    boxMax[3]      = {50.0f, 100.0f, 50.0f};
    float    spawnVelocity[3] = {0.0f, 0.0f, 0.0f};
    float    spawnRadius    = 30.0f;
    uint32_t workGroupSize = 256;

    // ── Phase 2A：固定步长（FixedTimestepAccumulator 参数）──
    float    fixedDt        = 1.0f / 60.0f;  ///< 每个仿真子步的固定时长
    uint32_t maxSubSteps    = 4;             ///< 单帧子步上限（螺旋式死亡保护）

    // ── Phase 4：碰撞后端 ──
    /// 默认 BruteForce：保证既有调用方行为零变化；Ring7 等价验证通过后由调用方显式切换
    GPUCollisionBackend collisionBackend = GPUCollisionBackend::BruteForce;
    float    spatialCellSize   = 2.0f;      ///< cell 边长；须 ≥ 2×maxParticleRadius（默认粒子半径分布 0.2~1.0）
    uint32_t spatialMaxCells   = 65536;    ///< cell 总量上限：单工作组扫描约束（超出时自动放大 cellSize）
};

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎统计
// ═══════════════════════════════════════════════════════════
struct GPUPhysicsStats {
    uint32_t totalParticles  = 0;
    uint32_t activeParticles = 0;
    float    avgFPS          = 0.0f;
    float    computeTimeMs   = 0.0f;
    uint64_t frameCount      = 0;
    uint64_t stepCount       = 0;   ///< 累计固定子步数
    double   simulationTime  = 0.0; ///< 累计仿真时间（秒，= stepCount * fixedDt）
};

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎（主类）
//
// Phase 2 局部契约（工程语义，非全引擎抽象）：
//   生命周期 : Initialize → (Reset / Update* / Render* ) → Shutdown
//   状态     : 双缓冲 Ping-Pong；GetStateBuffer() 恒指向"最近一次
//              完成步"的稳定快照，消费者（渲染/回读）只读该槽位
//   时间     : Update(frameDt) 内部由 FixedTimestepAccumulator
//              决定 0..maxSubSteps 个固定子步；仿真时间与帧率解耦
//   步语义   : 一个子步 = Integrate(cur→nxt) + Collide(nxt'→cur)
//              （src/dst 分离，Jacobi 式确定性求解）
// ═══════════════════════════════════════════════════════════
class GPUPhysicsEngine {
public:
    GPUPhysicsEngine();
    ~GPUPhysicsEngine();

    GPUPhysicsEngine(const GPUPhysicsEngine&) = delete;
    GPUPhysicsEngine& operator=(const GPUPhysicsEngine&) = delete;

    bool Initialize(RHI::IRHIDevice* device, const GPUPhysicsConfig& config);
    void Shutdown();

    /// 帧驱动入口：内部按累加器执行 0..N 个固定子步（含 Ping-Pong）
    void Update(float frameDt, RHI::IRHICommandList* cmdList);

    // ── 分 Pass 显式录制（外部剖析 / Benchmark 用，不推进槽位）──
    /// 半隐式欧拉积分 + 边界碰撞：读 src 写 dst（含前后 UAV 屏障）
    void RecordIntegratePass(float dt,
                             RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                             RHI::IRHICommandList* cmdList);
    /// 粒子间碰撞（Jacobi 快照求解）：读 src 写 dst（含前后 UAV 屏障）
    /// 后端由 config.collisionBackend 决定；spatial 后端自动执行 broad-phase
    void RecordCollidePass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                           RHI::IRHICommandList* cmdList);
    uint32_t GetDispatchGroupCount() const;

    void Render(RHI::IRHICommandList* renderCmdList);

    bool IsValid() const { return m_Initialized; }
    const GPUPhysicsConfig& GetConfig() const { return m_Config; }
    const GPUPhysicsStats& GetStats() const { return m_Stats; }
    uint32_t GetParticleCount() const { return m_Config.particleCount; }

    // ── Phase 2 契约查询 ──
    /// 最近一次完成步的稳定状态缓冲区（消费者只读）
    RHI::IRHIBuffer* GetStateBuffer() const;
    /// 下一子步的写入目标缓冲区（消费者不可依赖其内容）
    RHI::IRHIBuffer* GetScratchBuffer() const;
    /// 兼容别名 = GetStateBuffer()
    RHI::IRHIBuffer* GetParticleBuffer() const;
    float  GetAlpha() const { return m_Accumulator.GetAlpha(); }
    double GetSimulationTime() const { return m_Stats.simulationTime; }
    /// 最近一次 Update 实际执行的子步数（0 表示该帧未步进）
    uint32_t GetLastFrameSubsteps() const { return m_LastFrameSubsteps; }
    /// [Phase 4 调试] 辅助缓冲访问：0=cellCount 1=cellStart 2=cellOf 3=offset 4=sortedIdx
    RHI::IRHIBuffer* GetDebugAuxBuffer(uint32_t index) const;

    void UploadInitialData(const GPUParticleData* data, uint32_t count);
    void ReadbackParticles(uint32_t startIndex, uint32_t count, GPUParticleData* outData);
    void ResetParticles();

private:
    bool CreateParticleBuffers();
    bool CreateComputeShaders();
    bool CreateRenderResources();
    /// 初始化 spatial hash 网格参数（dims/cellSize 自适应，受 spatialMaxCells 约束）
    bool SetupSpatialGrid();
    /// 单个固定子步：Integrate(cur→nxt) + flip + [BroadPhase] + Collide(cur→nxt) + flip
    void RecordStep(RHI::IRHICommandList* cmdList);

    // ── Spatial Hash 分 Pass（broad-phase，作用于 src 快照；仅依赖 atomicAdd）──
    void RecordHashClearPass(RHI::IRHICommandList* cmdList);
    void RecordHashBuildPass(RHI::IRHIBuffer* src, RHI::IRHICommandList* cmdList);
    void RecordHashScanPass(RHI::IRHICommandList* cmdList);
    void RecordHashScatterPass(RHI::IRHICommandList* cmdList);
    void RecordCollideSpatialPass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                  RHI::IRHICommandList* cmdList);
    void RecordCollideBrutePass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                RHI::IRHICommandList* cmdList);
    void FlipSlots() { m_CurSlot ^= 1u; }

    // ── RHI 资源（GL46Buffer 持久映射在 stub 模式下也分配内存） ──
    std::shared_ptr<RHI::IRHIBuffer> m_StateBuffer[2];  ///< Ping-Pong 双缓冲

    // ── 其余成员变量 ──
    bool m_Initialized = false;
    uint32_t m_CurSlot = 0;          ///< 当前稳定状态所在槽位
    uint32_t m_LastFrameSubsteps = 0;
    GPUPhysicsConfig m_Config;
    GPUPhysicsStats  m_Stats;
    FixedTimestepAccumulator m_Accumulator{1.0f / 60.0f, 4};
    RHI::IRHIDevice* m_Device = nullptr;

    std::shared_ptr<RHI::IRHIBuffer> m_VertexBuffer;
    std::shared_ptr<RHI::IRHIBuffer> m_IndexBuffer;
    RHI::IRHIPipelineState* m_IntegratePSO = nullptr;
    RHI::IRHIPipelineState* m_CollidePSO   = nullptr;      ///< brute-force collide
    RHI::IRHIPipelineState* m_RenderPSO    = nullptr;

    // ── Phase 4：Spatial Hash 资源（计数排序管线，仅依赖 atomicAdd）──
    std::shared_ptr<RHI::IRHIBuffer> m_CellCountBuf;   ///< gridCells × 4B 每 cell 粒子数
    std::shared_ptr<RHI::IRHIBuffer> m_CellStartBuf;   ///< gridCells × 4B 排他前缀和
    std::shared_ptr<RHI::IRHIBuffer> m_CellOfBuf;      ///< N × 4B 每粒子所属 cell
    std::shared_ptr<RHI::IRHIBuffer> m_OffsetBuf;      ///< N × 4B cell 内序号
    std::shared_ptr<RHI::IRHIBuffer> m_SortedIdxBuf;   ///< N × 4B 按 cell 分组的粒子索引
    RHI::IRHIPipelineState* m_HashClearPSO   = nullptr;
    RHI::IRHIPipelineState* m_HashBuildPSO   = nullptr;
    RHI::IRHIPipelineState* m_HashScanPSO    = nullptr;
    RHI::IRHIPipelineState* m_HashScatterPSO = nullptr;
    RHI::IRHIPipelineState* m_CollideSpatialPSO = nullptr;
    uint32_t m_GridDim[3]  = {0, 0, 0};                    ///< 三轴 cell 数
    float    m_CellSize    = 2.0f;                         ///< 自适应后的 cell 边长
    uint32_t m_GridCells   = 0;                            ///< 总 cell 数

    uint32_t m_IndexCount = 0;
    float m_ElapsedTime = 0.0f;
};

} // namespace Engine