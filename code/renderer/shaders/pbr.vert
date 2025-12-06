#version 120

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texCoord;

void main() {
    v_position = (gl_ModelViewMatrix * gl_Vertex).xyz;
    v_normal = gl_NormalMatrix * gl_Normal;
    v_texCoord = gl_MultiTexCoord0.st;
    gl_Position = ftransform();
}
