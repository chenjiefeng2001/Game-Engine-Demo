#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 u_MVP;
uniform mat4 u_Model;

out vec3 v_FragPos;
out vec3 v_Normal;

void main() {
    gl_Position = u_MVP * vec4(aPos, 1.0);
    v_FragPos = vec3(u_Model * vec4(aPos, 1.0));
    v_Normal = mat3(u_Model) * aNormal;
}