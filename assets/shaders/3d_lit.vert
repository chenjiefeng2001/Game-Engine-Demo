#version 460 core
layout(location=0)in vec3 aPos;layout(location=1)in vec3 aNormal;
layout(location=2)in vec2 aTexCoord;layout(location=3)in vec3 aTangent;
uniform mat4 u_Model,u_View,u_Projection,u_NormalMatrix,u_MVP;
uniform mat4 u_LightSpaceMatrix;uniform float u_DisplacementStrength=0;
uniform sampler2D u_HeightMap;uniform bool u_HasHeightMap;
out vec3 v_WorldPos,v_ViewPos,v_Normal,v_Tangent,v_Bitangent,v_ViewDir;
out vec2 v_TexCoord;out float v_Displacement;out vec4 v_LightSpacePos;
void main(){
    vec4 worldPos=u_Model*vec4(aPos,1);
    float disp=0;
    if(u_HasHeightMap&&u_DisplacementStrength>0){
        float h=texture(u_HeightMap,aTexCoord).r;
        disp=(h-0.5)*u_DisplacementStrength;
        worldPos.xyz+=normalize(mat3(u_NormalMatrix)*aNormal)*disp;
    }
    v_Displacement=disp;v_WorldPos=worldPos.xyz;v_ViewPos=(u_View*worldPos).xyz;
    v_Normal=mat3(u_NormalMatrix)*aNormal;
    if(length(aTangent)>0.001){
        v_Tangent=normalize(mat3(u_Model)*aTangent);
        v_Bitangent=normalize(cross(v_Normal,v_Tangent));
    }else{v_Tangent=mat3(u_Model)*normalize(cross(aNormal,vec3(0,0,1)));v_Bitangent=cross(v_Normal,v_Tangent);}
    v_TexCoord=aTexCoord;v_ViewDir=(u_View*vec4(0,0,0,1)-u_View*worldPos).xyz;
    v_LightSpacePos=u_LightSpaceMatrix*worldPos;
    gl_Position=u_MVP*vec4(aPos,1);
}
