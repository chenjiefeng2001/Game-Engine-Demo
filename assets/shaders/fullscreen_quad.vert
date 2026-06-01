#version 460 core
// 全屏四边形 — 用于延迟渲染光照通道
layout(location=0)in vec3 aPos;
layout(location=2)in vec2 aTexCoord;
out vec2 v_TexCoord;
void main(){
    v_TexCoord=aTexCoord;
    gl_Position=vec4(aPos,1);
}
