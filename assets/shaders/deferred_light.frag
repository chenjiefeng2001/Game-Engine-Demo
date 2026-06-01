#version 460 core
// ═══════════════════════════════════════════════
// 延迟渲染 — 光照通道 Fragment Shader
// 读取 G-Buffer 纹理，逐像素计算光照
// ═══════════════════════════════════════════════
in vec2 v_TexCoord;
out vec4 FragColor;

// G-Buffer 纹理
uniform sampler2D u_PositionMap;   // RGB = world position
uniform sampler2D u_NormalMap;     // RGB = normal*0.5+0.5, A = roughness
uniform sampler2D u_AlbedoMap;     // RGB = diffuse, A = metallic
uniform sampler2D u_AOMap;         // R = ao, G = specular
uniform sampler2D u_DepthMap;      // depth

uniform vec3 u_ViewPos;
uniform vec3 u_AmbientColor;
uniform mat4 u_View;

// 光源
#define MAX_LIGHTS 4
uniform int  u_LightCount;
uniform vec3 u_LightPos[MAX_LIGHTS];
uniform vec3 u_LightColor[MAX_LIGHTS];
uniform float u_LightIntensity[MAX_LIGHTS];

// 阴影
uniform sampler2D u_ShadowMap;
uniform mat4 u_LightSpaceMatrix;
uniform float u_ShadowBias=0.005;
uniform bool u_ShadowEnabled=false;
uniform bool u_DebugGBuffer=false;

vec3 WorldPosFromGBuffer(vec2 uv){
    return texture(u_PositionMap,uv).rgb;
}
vec3 GetNormal(vec2 uv){
    return normalize(texture(u_NormalMap,uv).rgb*2-1);
}
vec4 GetAlbedo(vec2 uv){
    return texture(u_AlbedoMap,uv);
}
float GetAO(vec2 uv){return texture(u_AOMap,uv).r;}
float GetSpecular(vec2 uv){return texture(u_AOMap,uv).g;}
float GetRoughness(vec2 uv){return texture(u_NormalMap,uv).a;}

// PCF 阴影
float Shadow(vec3 posWS){
    if(!u_ShadowEnabled)return 0;
    vec4 lp=u_LightSpaceMatrix*vec4(posWS,1);
    vec3 proj=lp.xyz/lp.w*0.5+0.5;
    if(proj.x<0||proj.x>1||proj.y<0||proj.y>1||proj.z>1)return 0;
    float cur=proj.z-u_ShadowBias;
    float shadow=0; vec2 ts=1.0/textureSize(u_ShadowMap,0);
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)
        shadow+=cur>texture(u_ShadowMap,proj.xy+vec2(x,y)*ts).r?1:0;
    return shadow/9.0;
}

vec3 CalcLight(vec3 lPos,vec3 lCol,float intensity,vec3 P,vec3 N,vec3 V,vec3 base,float specStr,float roughness){
    vec3 L=normalize(lPos-P); vec3 H=normalize(L+V);
    float dist=length(lPos-P); float atten=1/(1+0.07*dist+0.03*dist*dist);
    float diff=max(dot(N,L),0);
    float shin=pow(2,8*(1-roughness));
    float spec=pow(max(dot(N,H),0),shin)*specStr;
    return (diff*lCol+spec*lCol)*intensity*atten*base;
}

void main(){
    vec2 uv=v_TexCoord;
    vec3 P=WorldPosFromGBuffer(uv);
    if(length(P)<0.001){FragColor=vec4(u_AmbientColor,1);return;}
    vec3 N=GetNormal(uv);
    vec4 albedo=GetAlbedo(uv);
    float ao=GetAO(uv);
    float specStr=GetSpecular(uv);
    float roughness=GetRoughness(uv);
    vec3 V=normalize(u_ViewPos-P);

    // 环境光 * AO
    vec3 result=u_AmbientColor*albedo.rgb*ao;

    // 光源
    for(int i=0;i<MAX_LIGHTS;i++){
        if(i>=u_LightCount)break;
        result+=CalcLight(u_LightPos[i],u_LightColor[i],u_LightIntensity[i],
                          P,N,V,albedo.rgb,specStr,roughness);
    }

    // 阴影
    result*=(1-Shadow(P)*0.6);

    FragColor=vec4(result,albedo.a);
}
