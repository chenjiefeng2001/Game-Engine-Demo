/**
 * @file GPUPhysicsEngine.cpp
 * @brief GPU 物理引擎 MVP 实现 — Compute Shader 驱动的海量球体离散元模拟
 *
 * 此实现验证以下引擎能力：
 *   1. Compute Shader 的创建与 Dispatch
 *   2. SSBO 在 Compute 与 Graphics 管线间的共享（Zero-Copy 渲染）
 *   3. Memory Barrier 的正确性（防止读写竞争）
 *   4. 实例化渲染（Instanced Rendering）
 *
 * 本文件包含完整的 OpenGL 实现。Vulkan 版本可基于
 * CreateVulkanComputePipeline() 辅助函数编写。
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/FileSystem.h"
#include <cstring>
#include <cmath>
#include <random>
#include <sstream>

// OpenGL 头文件
#include <glad/gl.h>

namespace Engine {

static Logger s_Log("GPUPhysics");

// ═══════════════════════════════════════════════════════════
// 着色器源代码（内嵌版本，以备文件加载失败时的后备方案）
// ═══════════════════════════════════════════════════════════
// 实际运行时优先从 assets/shaders/ 加载 .glsl 文件

// ═══════════════════════════════════════════════════════════
// 辅助函数
// ═══════════════════════════════════════════════════════════

/**
 * @brief 从文件加载着色器源码
 * @param filepath 文件路径
 * @return GLSL 源码字符串，失败返回空字符串
 */
static std::string LoadShaderSource(const std::string& filepath) {
    auto data = FileSystem::ReadFile(filepath);
    if (data.empty()) {
        s_Log.Error("Failed to load shader: {}", filepath);
        return {};
    }
    return std::string(reinterpret_cast<char*>(data.data()), data.size());
}

/**
 * @brief 编译 GLSL 着色器
 * @param type 着色器类型（GL_COMPUTE_SHADER / GL_VERTEX_SHADER / GL_FRAGMENT_SHADER）
 * @param source GLSL 源码
 * @return 着色器对象 ID，失败返回 0
 */
static uint32_t CompileGLShader(GLenum type, const std::string& source) {
    if (source.empty()) return 0;

    uint32_t shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // 检查编译结果
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        s_Log.Error("Shader compile error: {}", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

/**
 * @brief 编译着色器程序（Compute Shader 单阶段）
 * @param csSource Compute Shader 源码
 * @return 程序对象 ID，失败返回 0
 */
static uint32_t CreateComputeProgram(const std::string& csSource) {
    uint32_t cs = CompileGLShader(GL_COMPUTE_SHADER, csSource);
    if (!cs) return 0;

    uint32_t program = glCreateProgram();
    glAttachShader(program, cs);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        s_Log.Error("Compute program link error: {}", log);
        glDeleteProgram(program);
        glDeleteShader(cs);
        return 0;
    }

    // 分离后可删除 Shader 对象
    glDetachShader(program, cs);
    glDeleteShader(cs);

    return program;
}

/**
 * @brief 编译着色器程序（渲染管线：Vertex + Fragment）
 * @param vsSource Vertex Shader 源码
 * @param fsSource Fragment Shader 源码
 * @return 程序对象 ID，失败返回 0
 */
static uint32_t CreateRenderProgram(const std::string& vsSource,
                                     const std::string& fsSource) {
    uint32_t vs = CompileGLShader(GL_VERTEX_SHADER, vsSource);
    uint32_t fs = CompileGLShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    uint32_t program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        s_Log.Error("Render program link error: {}", log);
        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}

/**
 * @brief 生成单位球体网格
 * @param outVBO 输出 VBO ID
 * @param outIBO 输出 IBO ID
 * @param outIndexCount 输出索引数
 * @param subdivisions 细分次数（2 = ~128 三角形，3 = ~512 三角形）
 */
static void GenerateSphereMesh(uint32_t& outVBO, uint32_t& outIBO,
                                uint32_t& outIndexCount, int subdivisions = 2) {
    std::vector<float> vertices;   // 交错: pos(3) + normal(3)
    std::vector<uint32_t> indices;

    // 从二十面体开始细分（Icosahedron）
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    // 12 个顶点的二十面体
    struct Vec3 { float x, y, z; };
    Vec3 baseVerts[] = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };

    // 20 个三角形
    uint32_t baseIndices[] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    // 标准化顶点
    for (auto& v : baseVerts) {
        float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        v.x /= len; v.y /= len; v.z /= len;
    }

    // 顶点映射（用于细分）
    struct EdgeKey {
        uint32_t a, b;
        bool operator<(const EdgeKey& o) const {
            if (a != o.a) return a < o.a;
            return b < o.b;
        }
    };

    std::map<EdgeKey, uint32_t> edgeMap;
    std::vector<Vec3> vertVec;
    for (auto& v : baseVerts) vertVec.push_back(v);
    std::vector<uint32_t> idxVec;
    for (auto i : baseIndices) idxVec.push_back(i);

    // 细分
    for (int s = 0; s < subdivisions; ++s) {
        edgeMap.clear();
        std::vector<uint32_t> newIdx;

        auto getMidpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            EdgeKey key = {std::min(a, b), std::max(a, b)};
            auto it = edgeMap.find(key);
            if (it != edgeMap.end()) return it->second;

            Vec3 mid = {
                (vertVec[a].x + vertVec[b].x) * 0.5f,
                (vertVec[a].y + vertVec[b].y) * 0.5f,
                (vertVec[a].z + vertVec[b].z) * 0.5f,
            };
            // 投影到单位球面
            float len = std::sqrt(mid.x*mid.x + mid.y*mid.y + mid.z*mid.z);
            mid.x /= len; mid.y /= len; mid.z /= len;

            uint32_t idx = static_cast<uint32_t>(vertVec.size());
            vertVec.push_back(mid);
            edgeMap[key] = idx;
            return idx;
        };

        for (size_t i = 0; i < idxVec.size(); i += 3) {
            uint32_t a = idxVec[i], b = idxVec[i+1], c = idxVec[i+2];
            uint32_t ab = getMidpoint(a, b);
            uint32_t bc = getMidpoint(b, c);
            uint32_t ca = getMidpoint(c, a);

            newIdx.push_back(a); newIdx.push_back(ab); newIdx.push_back(ca);
            newIdx.push_back(b); newIdx.push_back(bc); newIdx.push_back(ab);
            newIdx.push_back(c); newIdx.push_back(ca); newIdx.push_back(bc);
            newIdx.push_back(ab); newIdx.push_back(bc); newIdx.push_back(ca);
        }

        idxVec = std::move(newIdx);
    }

    // 构建顶点缓冲区数据（位置 + 法线 = 6 floats per vertex）
    vertices.reserve(vertVec.size() * 6);
    for (auto& v : vertVec) {
        vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z);
        vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z); // 法线 = 归一化位置
    }

    indices = std::move(idxVec);
    outIndexCount = static_cast<uint32_t>(indices.size());

    // 创建 OpenGL 缓冲区
    glGenBuffers(1, &outVBO);
    glBindBuffer(GL_ARRAY_BUFFER, outVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &outIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint32_t),
                 indices.data(), GL_STATIC_DRAW);
}

// ═══════════════════════════════════════════════════════════
// GPUPhysicsEngine 实现
// ═══════════════════════════════════════════════════════════

GPUPhysicsEngine::GPUPhysicsEngine() = default;
GPUPhysicsEngine::~GPUPhysicsEngine() { Shutdown(); }

bool GPUPhysicsEngine::Initialize(const GPUPhysicsConfig& config) {
    if (m_Initialized) Shutdown();

    m_Config = config;
    s_Log.Info("Initializing GPU Physics Engine with {} particles",
               config.particleCount);

    // 1. 创建粒子 SSBO
    if (!CreateParticleBuffer()) {
        s_Log.Error("Failed to create particle buffer");
        return false;
    }

    // 2. 编译 Compute Shaders
    if (!CompileComputeShaders()) {
        s_Log.Error("Failed to compile compute shaders");
        Shutdown();
        return false;
    }

    // 3. 编译渲染 Shaders
    if (!CompileRenderShader()) {
        s_Log.Error("Failed to compile render shader");
        Shutdown();
        return false;
    }

    // 4. 创建球体网格
    if (!CreateSphereMesh()) {
        s_Log.Error("Failed to create sphere mesh");
        Shutdown();
        return false;
    }

    // 5. 初始化粒子数据
    ResetParticles();

    m_Initialized = true;
    s_Log.Info("GPU Physics Engine initialized successfully");
    return true;
}

void GPUPhysicsEngine::Shutdown() {
    if (!m_Initialized) return;

    // 删除 OpenGL 资源
    if (m_SSBO)              glDeleteBuffers(1, &m_SSBO);
    if (m_VBO)               glDeleteBuffers(1, &m_VBO);
    if (m_IBO)               glDeleteBuffers(1, &m_IBO);
    if (m_VAO)               glDeleteVertexArrays(1, &m_VAO);
    if (m_IntegrateProgram)  glDeleteProgram(m_IntegrateProgram);
    if (m_CollideProgram)    glDeleteProgram(m_CollideProgram);
    if (m_RenderProgram)     glDeleteProgram(m_RenderProgram);

    m_SSBO = m_VBO = m_IBO = m_VAO = 0;
    m_IntegrateProgram = m_CollideProgram = m_RenderProgram = 0;
    m_IndexCount = 0;
    m_Initialized = false;
    m_Stats = {};

    s_Log.Info("GPU Physics Engine shutdown");
}

void GPUPhysicsEngine::Update(float dt) {
    if (!m_Initialized) return;

    m_Stats.frameCount++;

    // ── Pass 1: 积分（半隐式欧拉 + 边界碰撞） ──
    glUseProgram(m_IntegrateProgram);

    // 设置 Uniforms
    glUniform1f(glGetUniformLocation(m_IntegrateProgram, "u_DeltaTime"), dt);
    glUniform3f(glGetUniformLocation(m_IntegrateProgram, "u_Gravity"),
                m_Config.gravity[0], m_Config.gravity[1], m_Config.gravity[2]);
    glUniform3f(glGetUniformLocation(m_IntegrateProgram, "u_BoxMin"),
                m_Config.boxMin[0], m_Config.boxMin[1], m_Config.boxMin[2]);
    glUniform3f(glGetUniformLocation(m_IntegrateProgram, "u_BoxMax"),
                m_Config.boxMax[0], m_Config.boxMax[1], m_Config.boxMax[2]);
    glUniform1f(glGetUniformLocation(m_IntegrateProgram, "u_Restitution"),
                m_Config.restitution);
    glUniform1f(glGetUniformLocation(m_IntegrateProgram, "u_Damping"),
                m_Config.damping);

    // 绑定 SSBO
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO);

    // Dispatch
    uint32_t groupCount = (m_Config.particleCount + m_Config.workGroupSize - 1)
                          / m_Config.workGroupSize;
    glDispatchCompute(groupCount, 1, 1);

    // ── 屏障: ComputeWrite → ComputeRead（确保积分完成后再碰撞） ──
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── Pass 2: 碰撞检测 + 惩罚力响应 ──
    glUseProgram(m_CollideProgram);

    glUniform1f(glGetUniformLocation(m_CollideProgram, "u_DeltaTime"), dt);
    glUniform1f(glGetUniformLocation(m_CollideProgram, "u_Stiffness"),
                m_Config.stiffness);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO);
    glDispatchCompute(groupCount, 1, 1);

    // ── 屏障: ComputeWrite → VertexRead（确保碰撞完成后才渲染） ──
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPUPhysicsEngine::Render() {
    if (!m_Initialized || m_Stats.frameCount == 0) return;

    glUseProgram(m_RenderProgram);

    // 绑定 VAO
    glBindVertexArray(m_VAO);

    // 绑定 SSBO（binding = 0）
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO);

    // 实例化绘制
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(m_IndexCount),
                            GL_UNSIGNED_INT,
                            nullptr,
                            static_cast<GLsizei>(m_Config.particleCount));

    glBindVertexArray(0);
}

void GPUPhysicsEngine::ResetParticles() {
    if (!m_SSBO) return;

    // 在 CPU 上生成初始数据
    std::vector<GPUParticleData> initialData(m_Config.particleCount);

    // 随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-m_Config.spawnRadius, m_Config.spawnRadius);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    for (uint32_t i = 0; i < m_Config.particleCount; ++i) {
        auto& p = initialData[i];

        // 随机位置（在球体内均匀分布）
        p.position[0] = posDist(gen);
        p.position[1] = std::abs(posDist(gen)) + 10.0f;  // 确保在地面上方
        p.position[2] = posDist(gen);
        p.radius = radiusDist(gen);
        p.velocity[0] = m_Config.spawnVelocity[0] + posDist(gen) * 0.5f;
        p.velocity[1] = m_Config.spawnVelocity[1] + std::abs(posDist(gen) * 0.3f);
        p.velocity[2] = m_Config.spawnVelocity[2] + posDist(gen) * 0.5f;
        p.mass = massDist(gen);
        p.color[0] = colorDist(gen);
        p.color[1] = colorDist(gen);
        p.color[2] = colorDist(gen);
        p.color[3] = 1.0f;

        // padding 已自动归零
    }

    // 上传到 GPU SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    initialData.size() * sizeof(GPUParticleData),
                    initialData.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    s_Log.Info("Particle data uploaded: {} particles", m_Config.particleCount);
}

// ═══════════════════════════════════════════════════════════
// 内部初始化辅助
// ═══════════════════════════════════════════════════════════

bool GPUPhysicsEngine::CreateParticleBuffer() {
    // SSBO: 使用 GPU_WRITE | GPU_READ | DYNAMIC_STORAGE
    // Compute Shader 需要读写，Graphics Shader 需要读取
    glGenBuffers(1, &m_SSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);

    // 分配显存（不初始化数据，ResetParticles 会填充）
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    m_Config.particleCount * sizeof(GPUParticleData),
                    nullptr,
                    GL_MAP_WRITE_BIT | GL_MAP_READ_BIT |
                    GL_DYNAMIC_STORAGE_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    s_Log.Info("Particle SSBO created: {} bytes",
               m_Config.particleCount * sizeof(GPUParticleData));
    return true;
}

bool GPUPhysicsEngine::CompileComputeShaders() {
    // ── 加载积分 Pass ──
    std::string integrateSource = LoadShaderSource("assets/shaders/gpu_physics_integrate.glsl");
    if (integrateSource.empty()) {
        s_Log.Error("Cannot load integrate shader, check assets/shaders/gpu_physics_integrate.glsl");
        return false;
    }

    m_IntegrateProgram = CreateComputeProgram(integrateSource);
    if (!m_IntegrateProgram) {
        s_Log.Error("Failed to compile integrate compute shader");
        return false;
    }
    s_Log.Info("Integrate compute shader compiled successfully");

    // ── 加载碰撞 Pass ──
    std::string collideSource = LoadShaderSource("assets/shaders/gpu_physics_collide.glsl");
    if (collideSource.empty()) {
        s_Log.Error("Cannot load collide shader, check assets/shaders/gpu_physics_collide.glsl");
        return false;
    }

    m_CollideProgram = CreateComputeProgram(collideSource);
    if (!m_CollideProgram) {
        s_Log.Error("Failed to compile collide compute shader");
        return false;
    }
    s_Log.Info("Collide compute shader compiled successfully");

    return true;
}

bool GPUPhysicsEngine::CompileRenderShader() {
    std::string vsSource = LoadShaderSource("assets/shaders/gpu_physics_render.vert");
    std::string fsSource = LoadShaderSource("assets/shaders/gpu_physics_render.frag");

    if (vsSource.empty() || fsSource.empty()) {
        s_Log.Error("Cannot load render shaders");
        return false;
    }

    m_RenderProgram = CreateRenderProgram(vsSource, fsSource);
    if (!m_RenderProgram) {
        s_Log.Error("Failed to compile render shader program");
        return false;
    }

    s_Log.Info("Render shader program compiled successfully");
    return true;
}

bool GPUPhysicsEngine::CreateSphereMesh() {
    // 生成球体网格
    GenerateSphereMesh(m_VBO, m_IBO, m_IndexCount, 2);  // 2 次细分 ≈ 128 triangles

    // 创建 VAO
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // 绑定 VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    // 位置 (location = 0): 3 floats, stride = 6 floats
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // 法线 (location = 1): 3 floats, stride = 6 floats, offset = 3 floats
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 绑定 IBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);

    glBindVertexArray(0);

    s_Log.Info("Sphere mesh created: {} vertices, {} indices",
               m_IndexCount * 3 / 2, m_IndexCount);
    return true;
}

} // namespace Engine