#version 460 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out int EntityID;

in vec2 v_TexCoord;
uniform sampler2D u_IconTex;
uniform int u_EntityID;

void main() {
    vec4 texColor = texture(u_IconTex, v_TexCoord);
    if(texColor.a < 0.1) discard; // 剔除透明像素，防止遮挡

    FragColor = texColor;
    EntityID = u_EntityID; // 写入 ID Buffer，使其可点击！
}