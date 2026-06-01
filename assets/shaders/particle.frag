#version 460 core
// 粒子/贴花 Fragment Shader
in vec2 v_TexCoord;
in vec4 v_Color;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform bool u_HasTexture;

void main(){
    vec4 tex=u_HasTexture?texture(u_Texture,v_TexCoord):vec4(1);
    FragColor=tex*v_Color;
    if(FragColor.a<0.01)discard;
}
