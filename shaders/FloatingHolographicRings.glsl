// FloatingHolographicRings.glsl
// Procedural holographic rings shader for macOS OpenGL

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

    float t = iTime * 0.5;

    // Multiple rotating rings
    float rings = 0.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float speed = 0.3 + fi * 0.15;
        float radius = 0.1 + fi * 0.08 + sin(t * speed + fi) * 0.02;
        float thickness = 0.005 + sin(t * 2.0 + fi) * 0.002;
        float ring = smoothstep(thickness, 0.0, abs(r - radius));
        ring *= 0.5 + 0.5 * sin(a * (3.0 + fi) + t * speed * 2.0);
        rings += ring * (1.0 - fi * 0.15);
    }

    // Audio reactivity
    rings *= 1.0 + iBass * 0.3;
    float pulse = 1.0 + iBeat * 0.2;

    // Holographic colors
    vec3 holoCol = vec3(
        0.5 + 0.5 * sin(a * 3.0 + t),
        0.5 + 0.5 * sin(a * 3.0 + t + 2.0),
        0.5 + 0.5 * sin(a * 3.0 + t + 4.0)
    );

    vec3 col = vec3(0.02, 0.02, 0.05);
    col += holoCol * rings * pulse;

    // Center glow
    float glow = 1.0 / (r * 10.0 + 1.0);
    col += vec3(0.3, 0.5, 0.8) * glow * (0.2 + iLevel * 0.3);

    // Scan lines
    col *= 0.9 + 0.1 * sin(uv.y * iResolution.y * 0.5);

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
