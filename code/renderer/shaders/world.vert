#version 120

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texCoord;
varying vec2 v_lightmapCoord;

void main() {
    v_position = (gl_ModelViewMatrix * gl_Vertex).xyz;
    v_normal = gl_NormalMatrix * gl_Normal;
    v_texCoord = gl_MultiTexCoord0.st;
    v_lightmapCoord = gl_MultiTexCoord1.st;
    gl_Position = ftransform();
}
