// SpeakerConeDistortion.glsl
// Port of SpeakerConeDistortion.hlsl for macOS OpenGL

uniform vec2 iResolution;
uniform float iTime;
uniform float iBeat;
uniform float iLevel;
uniform float iBass, iMid, iTreble;
uniform float iBpm;
uniform float iSongPosBeats;
varying vec2 vUV;

void main() {
    vec2 uv = vUV;
    float aspect = iResolution.x / iResolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);

    float r = length(p);
    float a = atan(p.y, p.x);

    // Concentric rings emanating from center, reacting to bass
    float rings = sin(r * 40.0 - iTime * 3.0 - iBeat * 8.0);
    rings = rings * 0.5 + 0.5;
    rings *= smoothstep(0.0, 0.1, r) * smoothstep(0.8, 0.5, r);

    // Speaker cone shape
    float cone = smoothstep(0.5, 0.45, r) * smoothstep(0.1, 0.12, r);

    // Distortion waves
    float wave = sin(a * 8.0 + iTime * 2.0) * 0.5 + 0.5;
    wave *= iBass * 0.3;

    // Colors
    vec3 baseCol = vec3(0.1, 0.1, 0.15);
    vec3 ringCol = vec3(0.3, 0.6, 1.0) * (0.5 + iLevel);
    vec3 hotCol = vec3(1.0, 0.4, 0.2) * iBass;

    vec3 col = baseCol;
    col = mix(col, ringCol, rings * 0.8);
    col += hotCol * wave * cone;

    // Center dust cap
    float cap = smoothstep(0.12, 0.10, r);
    col = mix(col, vec3(0.05, 0.05, 0.08), cap);

    // Outer rim
    float rim = smoothstep(0.48, 0.50, r) * smoothstep(0.52, 0.50, r);
    col += vec3(0.2, 0.2, 0.3) * rim;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
