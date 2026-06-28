#version 460 core
// 拾取 Pass Fragment Shader
// 将 Entity ID 写入 layout(location=1) 的拾取纹理
layout(location = 1) out int EntityID;

uniform int u_EntityID;

void main() {
    EntityID = u_EntityID;
}