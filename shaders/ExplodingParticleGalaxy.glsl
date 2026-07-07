// ExplodingParticleGalaxy.glsl
// Procedural particle galaxy shader for macOS OpenGL

uniform vec2 iResolution;
uniform float iTime;
uniform float iBeat;
uniform float iLevel;
uniform float iBass, iMid, iTreble;
uniform float iBpm;
uniform float iSongPosBeats;
varying vec2 vUV;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 p) {
    return vec2(hash(p), hash(p + vec2(31.7, 17.3)));
}

void main() {
    vec2 uv = vUV;
    float aspect = iResolution.x / iResolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);

    float t = iTime * 0.2;
    float r = length(p);

    // Spiral galaxy arms
    float acc = 0.0;
    vec3 col = vec3(0.01, 0.01, 0.03);

    for (int i = 0; i < 80; i++) {
        float fi = float(i);
        float angle = fi * 0.4 + t;
        float radius = fi * 0.008 + 0.02;
        vec2 starPos = vec2(cos(angle) * radius, sin(angle) * radius);
        starPos += (hash2(vec2(fi, 0.0)) - 0.5) * 0.05;

        float d = length(p - starPos);
        float bright = 0.01 / (d * d + 0.001);

        // Color based on distance from center
        vec3 starCol = mix(
            vec3(1.0, 0.8, 0.4),  // Warm center
            vec3(0.4, 0.6, 1.0),  // Cool edges
            radius / 0.5
        );

        col += starCol * bright * (0.5 + iLevel * 0.3);
        acc += bright;
    }

    // Core glow
    float core = 1.0 / (r * 8.0 + 0.05);
    col += vec3(1.0, 0.9, 0.6) * core * (0.3 + iBass * 0.4);

    // Beat pulse expansion
    float pulse = 1.0 + iBeat * 0.15;
    col *= pulse;

    // Background stars
    vec2 starUv = uv * 100.0;
    float star = step(0.995, fract(sin(dot(floor(starUv), vec2(12.9898, 78.233))) * 43758.5453));
    col += vec3(0.7, 0.8, 1.0) * star * 0.5;

    // Vignette
    col *= 1.0 - r * 0.3;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
