#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 u_ViewProj;   // 完整的 VP 矩阵
uniform vec3 u_CamRight;   // CPU 计算的世界空间相机右向量
uniform vec3 u_CamUp;      // CPU 计算的世界空间相机上向量
uniform vec3 u_WorldPos;
uniform vec2 u_Scale;

out vec2 v_TexCoord;

void main() {
    v_TexCoord = aTexCoord;

    // 构建始终面向摄像机的四边形顶点
    vec3 vertexPos = u_WorldPos
                   + u_CamRight * aPos.x * u_Scale.x
                   + u_CamUp    * aPos.y * u_Scale.y;

    gl_Position = u_ViewProj * vec4(vertexPos, 1.0);
}
