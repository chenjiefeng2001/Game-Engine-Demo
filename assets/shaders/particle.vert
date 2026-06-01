#version 460 core
// 粒子/贴花 Vertex Shader — 公告板渲染
layout(location=0)in vec3 aPos;
layout(location=1)in vec3 aNormal;
layout(location=2)in vec2 aTexCoord;
layout(location=3)in vec4 aColor;

uniform mat4 u_View,u_Projection;

out vec2 v_TexCoord;
out vec4 v_Color;

void main(){
    v_TexCoord=aTexCoord;
    v_Color=aColor;
    gl_Position=u_Projection*u_View*vec4(aPos,1);
}
