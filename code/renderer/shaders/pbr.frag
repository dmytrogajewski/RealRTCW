#version 120

uniform sampler2D u_diffuseMap;

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texCoord;

void main() {
    // Basic PBR placeholder
    vec4 color = texture2D(u_diffuseMap, v_texCoord);
    gl_FragColor = color;
}
