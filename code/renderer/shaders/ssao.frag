#version 120

uniform sampler2D u_depthTexture;
uniform sampler2D u_normalTexture;
uniform vec2 u_screenSize;
uniform float u_radius;
uniform float u_bias;
uniform float u_intensity;
uniform mat4 u_projectionMatrix;
uniform mat4 u_invProjectionMatrix;

varying vec2 v_texCoord;

vec3 getSampleVector(int i) {
    if (i==0) return vec3(0.5381, 0.1856, -0.4319);
    if (i==1) return vec3(0.1379, 0.2486, 0.4430);
    if (i==2) return vec3(0.3371, 0.5679, -0.0057);
    if (i==3) return vec3(-0.6999, -0.0451, -0.0019);
    if (i==4) return vec3(0.0689, -0.1598, -0.8547);
    if (i==5) return vec3(0.0560, 0.0069, -0.1843);
    if (i==6) return vec3(-0.0146, 0.1402, 0.0762);
    if (i==7) return vec3(0.0100, -0.1924, -0.0344);
    if (i==8) return vec3(-0.3577, -0.5301, -0.4358);
    if (i==9) return vec3(-0.3169, 0.1063, 0.0158);
    if (i==10) return vec3(0.0103, -0.5869, 0.0046);
    if (i==11) return vec3(-0.0897, -0.4940, 0.3287);
    if (i==12) return vec3(0.7119, -0.0154, -0.0918);
    if (i==13) return vec3(-0.0533, 0.0596, -0.5411);
    if (i==14) return vec3(0.0352, -0.0631, 0.5460);
    return vec3(-0.4776, 0.2847, -0.0271);
}

vec3 getViewPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = u_invProjectionMatrix * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    float depth = texture2D(u_depthTexture, v_texCoord).r;
    
    // If depth is 1.0 (far plane), no occlusion
    if (depth >= 0.999) {
        gl_FragColor = vec4(1.0);
        return;
    }
    
    // Reconstruct normal from depth (since we don't have a normal buffer)
    vec3 viewPos = getViewPos(v_texCoord, depth);
    vec3 ddx = dFdx(viewPos);
    vec3 ddy = dFdy(viewPos);
    vec3 normal = normalize(cross(ddx, ddy));
    
    float occlusion = 0.0;
    float radius = u_radius;
    
    for (int i = 0; i < 16; i++) {
        // Orient sample hemipshere along normal
        vec3 sampleVec = getSampleVector(i);
        // If sample is behind surface, flip it
        if (dot(sampleVec, normal) < 0.0) {
            sampleVec = -sampleVec;
        }
        
        vec3 samplePos = viewPos + sampleVec * radius;
        
        vec4 offset = u_projectionMatrix * vec4(samplePos, 1.0);
        offset.xy /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;
        
        // Bounds check
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) continue;
        
        float sampleDepth = texture2D(u_depthTexture, offset.xy).r;
        vec3 sampleViewPos = getViewPos(offset.xy, sampleDepth);
        
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleViewPos.z));
        
        if (sampleViewPos.z >= samplePos.z + u_bias) {
             occlusion += 1.0 * rangeCheck;
        }
    }
    
    occlusion = 1.0 - (occlusion / 16.0);
    occlusion = pow(occlusion, u_intensity);
    
    gl_FragColor = vec4(occlusion, occlusion, occlusion, 1.0);
}
