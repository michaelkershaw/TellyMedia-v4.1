// HypnoticSpiral.glsl
// Port of HypnoticSpiral.hlsl for macOS OpenGL

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

    // Spiral with audio reactivity
    float spiral = sin(a * 5.0 + r * 30.0 - iTime * 2.0 + iBeat * 3.0);
    spiral = spiral * 0.5 + 0.5;

    // Color cycling
    vec3 col1 = vec3(0.2, 0.4, 0.9);
    vec3 col2 = vec3(0.9, 0.3, 0.6);
    vec3 col3 = vec3(0.1, 0.9, 0.7);

    float t = iTime * 0.3;
    vec3 col = mix(col1, col2, sin(t) * 0.5 + 0.5);
    col = mix(col, col3, sin(t * 1.3 + 1.0) * 0.5 + 0.5);

    // Audio pulse
    float pulse = 1.0 + iBass * 0.5;
    col *= spiral * pulse;

    // Center glow
    float glow = 1.0 / (r * 8.0 + 1.0);
    col += vec3(0.5, 0.7, 1.0) * glow * (0.3 + iLevel * 0.5);

    // Vignette
    col *= 1.0 - r * 0.5;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
