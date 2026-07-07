// VibrantNebula.glsl
// Procedural nebula shader for macOS OpenGL

uniform vec2 iResolution;
uniform float iTime;
uniform float iBeat;
uniform float iLevel;
uniform float iBass, iMid, iTreble;
uniform float iBpm;
uniform float iSongPosBeats;
varying vec2 vUV;

// Simple 3D noise
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 uv = vUV;
    float aspect = iResolution.x / iResolution.y;
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);

    float t = iTime * 0.1 + iSongPosBeats * 0.05;

    // Layered FBM for nebula clouds
    vec3 q = vec3(p * 3.0, t);
    float n1 = fbm(q);
    float n2 = fbm(q + vec3(n1 * 2.0, 0.0, 0.0));
    float n3 = fbm(q + vec3(n2 * 2.0, n1, 0.0));

    // Colors
    vec3 col1 = vec3(0.4, 0.1, 0.6);  // Purple
    vec3 col2 = vec3(0.1, 0.3, 0.8);  // Blue
    vec3 col3 = vec3(0.8, 0.2, 0.4);  // Magenta
    vec3 hot = vec3(1.0, 0.8, 0.3);   // Gold highlight

    vec3 col = mix(col1, col2, n2);
    col = mix(col, col3, n3 * 0.5);
    col += hot * pow(n1, 3.0) * (0.5 + iBass * 0.5);

    // Audio reactivity
    col *= 0.8 + iLevel * 0.4;
    col += vec3(0.1, 0.05, 0.15) * iBass;

    // Stars
    vec2 starUv = uv * 200.0;
    float star = step(0.998, fract(sin(dot(floor(starUv), vec2(12.9898, 78.233))) * 43758.5453));
    col += vec3(1.0) * star * (0.5 + iTreble * 0.5);

    // Vignette
    float vig = 1.0 - length(p) * 0.6;
    col *= vig;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
