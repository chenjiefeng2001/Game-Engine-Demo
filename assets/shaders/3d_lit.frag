#version 460 core
in vec3 v_WorldPos,v_ViewPos,v_Normal,v_Tangent,v_Bitangent,v_ViewDir;
in vec2 v_TexCoord;in float v_Displacement;in vec4 v_LightSpacePos;
out vec4 FragColor;

uniform sampler2D u_Texture;uniform bool u_HasTexture;uniform vec4 u_ObjectColor;
uniform sampler2D u_NormalMap;uniform bool u_HasNormalMap;uniform float u_NormalStrength=1;
uniform sampler2D u_HeightMap;uniform bool u_HasHeightMap;
uniform float u_ParallaxScale=0.05,u_Shininess=32,u_SpecularStrength=0.5;
uniform vec3 u_ViewPos,u_AmbientColor;

#define MAX_LIGHTS 4
uniform int u_LightCount;uniform vec3 u_LightPos[MAX_LIGHTS];
uniform vec3 u_LightColor[MAX_LIGHTS];uniform float u_LightIntensity[MAX_LIGHTS];

uniform sampler2D u_ShadowMap;uniform mat4 u_LightSpaceMatrix;
uniform float u_ShadowBias=0.005;uniform bool u_ShadowEnabled=false;
uniform sampler2D u_SSAOMap;uniform bool u_SSAOEnabled=false;uniform float u_SSAOStrength=1;

vec2 ParallaxOffset(sampler2D hm,vec2 uv,vec3 vTS){
    if(!u_HasHeightMap||u_ParallaxScale<=0)return uv;
    float l=mix(32.0,8.0,abs(dot(vec3(0,0,1),vTS)));
    float ld=1/l,cd=0;vec2 duv=vTS.xy*u_ParallaxScale/l;vec2 cuv=uv;
    float h=1-texture(hm,cuv).r;while(cd<h){cuv-=duv;h=1-texture(hm,cuv).r;cd+=ld;}
    vec2 puv=cuv+duv;float ad=h-cd,bd=(1-texture(hm,puv).r)-(cd-ld);
    return mix(cuv,puv,ad/(ad-bd));
}
vec3 GetNormal(vec2 uv){
    if(!u_HasNormalMap)return normalize(v_Normal);
    vec3 n=texture(u_NormalMap,uv).rgb*2-1;n.xy*=u_NormalStrength;n=normalize(n);
    vec3 N=normalize(v_Normal),T=normalize(v_Tangent),B=normalize(v_Bitangent);
    return normalize(mat3(T,B,N)*n);
}
float ShadowPCF(vec3 lp){
    if(!u_ShadowEnabled)return 0;
    vec3 p=lp*0.5+0.5;if(p.x<0||p.x>1||p.y<0||p.y>1||p.z>1)return 0;
    float cur=p.z-u_ShadowBias,s=0;vec2 ts=1.0/textureSize(u_ShadowMap,0);
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)s+=cur>texture(u_ShadowMap,p.xy+vec2(x,y)*ts).r?1:0;
    return s/9.0;
}
vec3 CalcLight(vec3 lP,vec3 lC,float I,vec3 P,vec3 N,vec3 V,vec3 base,float shin,float ss){
    vec3 L=normalize(lP-P);vec3 H=normalize(L+V);
    float d=length(lP-P);float a=1/(1+0.07*d+0.03*d*d);
    float diff=max(dot(N,L),0);float spec=pow(max(dot(N,H),0),shin)*ss;
    return (diff*lC+spec*lC)*I*a*base;
}
void main(){
    vec3 vTS=normalize(vec3(dot(normalize(u_ViewPos-v_WorldPos),normalize(v_Tangent)),
        dot(normalize(u_ViewPos-v_WorldPos),normalize(v_Bitangent)),
        dot(normalize(u_ViewPos-v_WorldPos),normalize(v_Normal))));
    vec2 uv=ParallaxOffset(u_HeightMap,v_TexCoord,vTS);
    vec3 N=GetNormal(uv);vec3 V=normalize(u_ViewPos-v_WorldPos);
    vec4 base=u_HasTexture?texture(u_Texture,uv):u_ObjectColor;
    float ao=1.0;if(u_SSAOEnabled)ao=mix(1.0,texture(u_SSAOMap,gl_FragCoord.xy/textureSize(u_SSAOMap,0)).r,u_SSAOStrength);
    vec3 result=u_AmbientColor*base.rgb*ao;
    for(int i=0;i<MAX_LIGHTS;i++){if(i>=u_LightCount)break;
        result+=CalcLight(u_LightPos[i],u_LightColor[i],u_LightIntensity[i],v_WorldPos,N,V,base.rgb,u_Shininess,u_SpecularStrength);}
    if(u_ShadowEnabled&&u_LightCount>0)result*=(1-ShadowPCF(v_LightSpacePos.xyz/v_LightSpacePos.w)*0.6);
    FragColor=vec4(result,base.a);
}
