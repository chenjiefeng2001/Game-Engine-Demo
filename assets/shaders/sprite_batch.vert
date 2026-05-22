#version 460 core



layout(location = 0) in vec2 aPos;     
layout(location = 1) in vec2 aTexCoord; 
layout(location = 2) in vec4 aColor;    

uniform mat4 u_ViewProjection;

out vec2 v_TexCoord;
out vec4 v_Color;

void main() {
    gl_Position = u_ViewProjection * vec4(aPos, 0.0, 1.0);
    v_TexCoord = aTexCoord;
    v_Color = aColor;
}
