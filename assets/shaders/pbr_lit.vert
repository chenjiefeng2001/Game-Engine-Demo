#version 460 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;
layout(location=3) in vec3 aTangent;

uniform mat4 u_Model, u_View, u_Projection, u_NormalMatrix, u_MVP;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Tangent;
out vec3 v_Bitangent;

void main() {
    vec4 worldPos = u_Model * vec4(aPos, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = mat3(u_NormalMatrix) * aNormal;
    v_TexCoord = aTexCoord;

    if (length(aTangent) > 0.001) {
        v_Tangent = normalize(mat3(u_Model) * aTangent);
        v_Bitangent = normalize(cross(v_Normal, v_Tangent));
    } else {
        v_Tangent = vec3(1, 0, 0);
        v_Bitangent = vec3(0, 1, 0);
    }

    gl_Position = u_MVP * vec4(aPos, 1.0);
}