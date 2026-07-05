#version 460 core
// SSAO — 屏幕空间环境光遮挡
in vec2 v_TexCoord;
out float FragColor;
uniform sampler2D u_DepthMap;
uniform sampler2D u_NormalMap;
uniform mat4 u_Projection;
uniform float u_Radius=0.5;
uniform float u_Power=1.5;

// 半球采样核
const int SAMPLE_COUNT=16;
vec3 samples[SAMPLE_COUNT];
// 噪声纹理（4x4 旋转噪声）
uniform vec4 u_Noise[4];

vec3 ViewPosFromDepth(vec2 uv,float depth){
    vec4 clip=vec4(uv*2-1,depth*2-1,1);
    vec4 view=inverse(u_Projection)*clip;
    return view.xyz/view.w;
}

void main(){
    // 初始化采样核（应在 CPU 端生成，这里简化使用固定值）
    // 实际项目中应使用随机旋转采样核
    float depth=texture(u_DepthMap,v_TexCoord).r;
    if(depth>=1) { FragColor=1; return; }
    vec3 viewPos=ViewPosFromDepth(v_TexCoord,depth);
    vec3 normal=normalize(texture(u_NormalMap,v_TexCoord).rgb*2-1);

    float occlusion=0;
    vec2 ts=1.0/textureSize(u_DepthMap,0);

    for(int i=0;i<SAMPLE_COUNT;i++){
        vec3 samplePos=viewPos+samples[i]*u_Radius;
        vec4 offset=u_Projection*vec4(samplePos,1);
        offset.xy/=offset.w;
        offset.xy=offset.xy*0.5+0.5;
        float sampleDepth=texture(u_DepthMap,offset.xy).r;
        float rangeCheck=smoothstep(0,1,u_Radius/abs(viewPos.z-sampleDepth*100));
        occlusion+=rangeCheck*step(sampleDepth,depth);
    }
    FragColor=pow(1-occlusion/SAMPLE_COUNT,u_Power);
}
