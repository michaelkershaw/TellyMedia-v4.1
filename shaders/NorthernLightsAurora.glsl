// NorthernLightsAurora.glsl
// Procedural aurora shader for macOS OpenGL

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

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), f.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
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

    float t = iTime * 0.3;

    // Aurora bands
    float band1 = fbm(vec2(p.x * 3.0 + t, p.y * 1.5 - t * 0.5));
    float band2 = fbm(vec2(p.x * 4.0 - t * 0.7, p.y * 2.0 + t * 0.3));

    // Vertical gradient for aurora curtain
    float curtain = smoothstep(0.0, 0.3, uv.y) * smoothstep(1.0, 0.5, uv.y);
    curtain *= 0.5 + 0.5 * sin(uv.y * 10.0 + band1 * 5.0 + t);

    // Colors
    vec3 green = vec3(0.2, 0.9, 0.4);
    vec3 cyan = vec3(0.2, 0.6, 1.0);
    vec3 purple = vec3(0.6, 0.2, 0.9);
    vec3 hot = vec3(0.9, 0.3, 0.5);

    vec3 col = vec3(0.02, 0.03, 0.06); // Dark sky

    col += green * curtain * band1 * (0.7 + iBass * 0.3);
    col += cyan * curtain * band2 * (0.5 + iMid * 0.3);
    col += purple * curtain * band1 * band2 * (0.3 + iTreble * 0.4);
    col += hot * pow(band1 * curtain, 3.0) * iBeat * 0.5;

    // Stars
    vec2 starUv = uv * 150.0;
    float star = step(0.997, fract(sin(dot(floor(starUv), vec2(12.9898, 78.233))) * 43758.5453));
    col += vec3(0.8, 0.9, 1.0) * star;

    // Ground glow
    float ground = smoothstep(0.15, 0.0, uv.y);
    col += vec3(0.05, 0.1, 0.05) * ground;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
