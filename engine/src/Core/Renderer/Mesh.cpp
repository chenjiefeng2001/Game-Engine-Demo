#include "Engine/Core/Renderer/Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    Mesh::Mesh(const std::vector<Vertex3D>& vertices,
               const std::vector<uint32>& indices)
        : m_Vertices(vertices)
        , m_Indices(indices)
    {
    }

    // ============================================================
    // 法线重算
    // ============================================================

    void Mesh::RecalculateNormals() {
        // 初始化所有法线为零
        for (auto& v : m_Vertices)
            v.normal = Vec3(0.0f, 0.0f, 0.0f);

        // 对于每个三角面，计算面法线并累加到三个顶点
        for (size_t i = 0; i < m_Indices.size(); i += 3) {
            if (i + 2 >= m_Indices.size()) break;

            uint32 i0 = m_Indices[i];
            uint32 i1 = m_Indices[i + 1];
            uint32 i2 = m_Indices[i + 2];

            if (i0 >= m_Vertices.size() || i1 >= m_Vertices.size() || i2 >= m_Vertices.size())
                continue;

            const auto& v0 = m_Vertices[i0].position;
            const auto& v1 = m_Vertices[i1].position;
            const auto& v2 = m_Vertices[i2].position;

            glm::vec3 p0(v0.x, v0.y, v0.z);
            glm::vec3 p1(v1.x, v1.y, v1.z);
            glm::vec3 p2(v2.x, v2.y, v2.z);

            glm::vec3 edge1 = p1 - p0;
            glm::vec3 edge2 = p2 - p0;
            glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

            if (glm::isnan(faceNormal.x) || glm::isnan(faceNormal.y) || glm::isnan(faceNormal.z))
                continue;

            m_Vertices[i0].normal.x += faceNormal.x;
            m_Vertices[i0].normal.y += faceNormal.y;
            m_Vertices[i0].normal.z += faceNormal.z;
            m_Vertices[i1].normal.x += faceNormal.x;
            m_Vertices[i1].normal.y += faceNormal.y;
            m_Vertices[i1].normal.z += faceNormal.z;
            m_Vertices[i2].normal.x += faceNormal.x;
            m_Vertices[i2].normal.y += faceNormal.y;
            m_Vertices[i2].normal.z += faceNormal.z;
        }

        // 归一化所有法线
        for (auto& v : m_Vertices) {
            glm::vec3 n(v.normal.x, v.normal.y, v.normal.z);
            n = glm::normalize(n);
            if (!glm::isnan(n.x) && !glm::isnan(n.y) && !glm::isnan(n.z)) {
                v.normal.x = n.x;
                v.normal.y = n.y;
                v.normal.z = n.z;
            } else {
                // 法线为零或 NaN → 用默认向上向量
                v.normal = Vec3(0.0f, 1.0f, 0.0f);
            }
        }

        // ── 计算切线 ──
        for (auto& v : m_Vertices)
            v.tangent = Vec3(0, 0, 0);

        for (size_t i = 0; i + 2 < m_Indices.size(); i += 3) {
            uint32 i0 = m_Indices[i], i1 = m_Indices[i+1], i2 = m_Indices[i+2];
            if (i0>=m_Vertices.size()||i1>=m_Vertices.size()||i2>=m_Vertices.size()) continue;
            const auto& v0=m_Vertices[i0], v1=m_Vertices[i1], v2=m_Vertices[i2];
            glm::vec3 p0(v0.position.x,v0.position.y,v0.position.z);
            glm::vec3 p1(v1.position.x,v1.position.y,v1.position.z);
            glm::vec3 p2(v2.position.x,v2.position.y,v2.position.z);
            glm::vec2 uv0(v0.texCoord.x,v0.texCoord.y);
            glm::vec2 uv1(v1.texCoord.x,v1.texCoord.y);
            glm::vec2 uv2(v2.texCoord.x,v2.texCoord.y);
            glm::vec3 e1=p1-p0, e2=p2-p0;
            glm::vec2 duv1=uv1-uv0, duv2=uv2-uv0;
            float det = duv1.x*duv2.y - duv1.y*duv2.x;
            if (std::abs(det) < 1e-8f) continue;
            glm::vec3 tangent = (e1*duv2.y - e2*duv1.y) / det;
            m_Vertices[i0].tangent.x+=tangent.x; m_Vertices[i0].tangent.y+=tangent.y; m_Vertices[i0].tangent.z+=tangent.z;
            m_Vertices[i1].tangent.x+=tangent.x; m_Vertices[i1].tangent.y+=tangent.y; m_Vertices[i1].tangent.z+=tangent.z;
            m_Vertices[i2].tangent.x+=tangent.x; m_Vertices[i2].tangent.y+=tangent.y; m_Vertices[i2].tangent.z+=tangent.z;
        }
        for (auto& v : m_Vertices) {
            glm::vec3 t(v.tangent.x,v.tangent.y,v.tangent.z);
            glm::vec3 n(v.normal.x,v.normal.y,v.normal.z);
            t = glm::normalize(t - n * glm::dot(n, t));
            if (!glm::isnan(t.x)&&!glm::isnan(t.y)&&!glm::isnan(t.z)) {
                v.tangent=Vec3(t.x,t.y,t.z);
            } else {
                glm::vec3 up(0,1,0);
                if (std::abs(glm::dot(n,up))>0.99f) up=glm::vec3(0,0,1);
                t=glm::normalize(glm::cross(n,up));
                v.tangent=Vec3(t.x,t.y,t.z);
            }
        }
    }

    // ============================================================
    // 预置几何体
    // ============================================================

    Mesh Mesh::CreateCube(float size) {
        float h = size / 2.0f;

        std::vector<Vertex3D> vertices = {
            // 前面 (Z+)   normal: (0,0,1)
            {{-h, -h,  h}, {0,0,1}, {0,0}},
            {{ h, -h,  h}, {0,0,1}, {1,0}},
            {{ h,  h,  h}, {0,0,1}, {1,1}},
            {{-h,  h,  h}, {0,0,1}, {0,1}},
            // 后面 (Z-)
            {{ h, -h, -h}, {0,0,-1}, {0,0}},
            {{-h, -h, -h}, {0,0,-1}, {1,0}},
            {{-h,  h, -h}, {0,0,-1}, {1,1}},
            {{ h,  h, -h}, {0,0,-1}, {0,1}},
            // 上面 (Y+)
            {{-h,  h,  h}, {0,1,0}, {0,0}},
            {{ h,  h,  h}, {0,1,0}, {1,0}},
            {{ h,  h, -h}, {0,1,0}, {1,1}},
            {{-h,  h, -h}, {0,1,0}, {0,1}},
            // 下面 (Y-)
            {{-h, -h, -h}, {0,-1,0}, {0,0}},
            {{ h, -h, -h}, {0,-1,0}, {1,0}},
            {{ h, -h,  h}, {0,-1,0}, {1,1}},
            {{-h, -h,  h}, {0,-1,0}, {0,1}},
            // 右面 (X+)
            {{ h, -h,  h}, {1,0,0}, {0,0}},
            {{ h, -h, -h}, {1,0,0}, {1,0}},
            {{ h,  h, -h}, {1,0,0}, {1,1}},
            {{ h,  h,  h}, {1,0,0}, {0,1}},
            // 左面 (X-)
            {{-h, -h, -h}, {-1,0,0}, {0,0}},
            {{-h, -h,  h}, {-1,0,0}, {1,0}},
            {{-h,  h,  h}, {-1,0,0}, {1,1}},
            {{-h,  h, -h}, {-1,0,0}, {0,1}},
        };

        std::vector<uint32> indices = {
            // 每个面两个三角形
             0, 1, 2,  2, 3, 0,    // 前面
             4, 5, 6,  6, 7, 4,    // 后面
             8, 9,10, 10,11, 8,    // 上面
            12,13,14, 14,15,12,    // 下面
            16,17,18, 18,19,16,    // 右面
            20,21,22, 22,23,20,    // 左面
        };

        return Mesh(vertices, indices);
    }

    Mesh Mesh::CreateSphere(float radius, int32 segments) {
        std::vector<Vertex3D> vertices;
        std::vector<uint32> indices;

        // 生成顶点：使用经纬度网格
        for (int32 lat = 0; lat <= segments; ++lat) {
            float theta = (float)lat / (float)segments * glm::pi<float>();
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (int32 lon = 0; lon <= segments; ++lon) {
                float phi = (float)lon / (float)segments * glm::two_pi<float>();
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                Vec3 normal(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
                Vec3 position(normal.x * radius, normal.y * radius, normal.z * radius);
                Vec2 texCoord((float)lon / (float)segments, (float)lat / (float)segments);

                vertices.push_back({position, normal, texCoord});
            }
        }

        // 生成索引
        for (int32 lat = 0; lat < segments; ++lat) {
            for (int32 lon = 0; lon < segments; ++lon) {
                uint32 first  = (uint32)(lat * (segments + 1) + lon);
                uint32 second = first + (uint32)(segments + 1);

                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(first + 1);

                indices.push_back(second);
                indices.push_back(second + 1);
                indices.push_back(first + 1);
            }
        }

        return Mesh(vertices, indices);
    }

    Mesh Mesh::CreatePlane(float width, float height) {
        float hw = width / 2.0f;
        float hh = height / 2.0f;

        std::vector<Vertex3D> vertices = {
            {{-hw, 0.0f, -hh}, {0,1,0}, {0,0}},
            {{ hw, 0.0f, -hh}, {0,1,0}, {1,0}},
            {{ hw, 0.0f,  hh}, {0,1,0}, {1,1}},
            {{-hw, 0.0f,  hh}, {0,1,0}, {0,1}},
        };

        std::vector<uint32> indices = {
            0, 1, 2,  2, 3, 0
        };

        return Mesh(vertices, indices);
    }

    Mesh Mesh::CreateCylinder(float radius, float height, int32 segments) {
        std::vector<Vertex3D> vertices;
        std::vector<uint32> indices;

        float halfHeight = height / 2.0f;

        // 侧面顶点
        for (int32 i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * glm::two_pi<float>();
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);

            Vec3 normal(cosT, 0.0f, sinT);
            Vec2 texCoord((float)i / (float)segments, 0.0f);
            vertices.push_back({{cosT * radius, -halfHeight, sinT * radius}, normal, texCoord});

            texCoord.y = 1.0f;
            vertices.push_back({{cosT * radius,  halfHeight, sinT * radius}, normal, texCoord});
        }

        // 侧面索引
        for (int32 i = 0; i < segments; ++i) {
            uint32 a = (uint32)(i * 2);
            uint32 b = (uint32)(i * 2 + 1);
            uint32 c = (uint32)((i + 1) * 2);
            uint32 d = (uint32)((i + 1) * 2 + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(b); indices.push_back(c); indices.push_back(d);
        }

        // 顶盖/底盖的顶点和索引比较简单，省略以保持简洁。
        // 实际项目中可补全，或直接使用 CreateSphere + 缩放。

        return Mesh(vertices, indices);
    }

} // namespace Engine
