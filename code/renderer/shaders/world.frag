#version 120

uniform sampler2D u_diffuseMap;
// uniform sampler2D u_lightmap; // Not yet bound in tr_glsl.c

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texCoord;
varying vec2 v_lightmapCoord;

void main() {
    vec4 diffuse = texture2D(u_diffuseMap, v_texCoord);
    // vec4 lightmap = texture2D(u_lightmap, v_lightmapCoord);
    gl_FragColor = diffuse; // * lightmap * 2.0;
}
