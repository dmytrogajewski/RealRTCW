#version 120

uniform sampler2D u_screenTexture;
uniform sampler2D u_ssaoTexture;
uniform float u_exposure;
uniform float u_gamma;
uniform float u_useSSAO;

varying vec2 v_texCoord;

vec3 reinhardToneMap(vec3 color) {
    return color / (1.0 + color);
}

void main() {
    vec3 hdrColor = texture2D(u_screenTexture, v_texCoord).rgb;
    
    if (u_useSSAO > 0.5) {
        float occlusion = texture2D(u_ssaoTexture, v_texCoord).r;
        hdrColor *= occlusion;
    }
    
    hdrColor *= pow(2.0, u_exposure);
    vec3 mapped = reinhardToneMap(hdrColor);
    vec3 finalColor = pow(mapped, vec3(1.0 / u_gamma));
    
    gl_FragColor = vec4(finalColor, 1.0);
}
