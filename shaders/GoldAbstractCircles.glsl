// GoldAbstractCircles.glsl
// Port of GoldAbstractCircles.hlsl for macOS OpenGL
// TellyMedia v4 Compatible

uniform vec2 iResolution;
uniform float iTime;
uniform float iBeat;
uniform float iLevel;
uniform float iBass, iMid, iTreble;
uniform float iBpm;
uniform float iSongPosBeats;
varying vec2 vUV;

// ---------- Hash helpers ----------
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash2(vec2 p) {
    return vec2(hash(p), hash(p + 57.31));
}

// ============================================================
//  BOKEH LAYER
// ============================================================
float bokehLayer(vec2 uv, float density, float minSize, float maxSize,
                 float blur, float rise, float seed, float t,
                 float audioPulse, out float brightnessOut) {
    brightnessOut = 0.0;
    float acc = 0.0;

    vec2 st = uv * density;
    st.y += t * rise;
    st.x += sin(t * 0.15 + seed * 3.0) * 0.15;

    vec2 id = floor(st);
    vec2 gv = fract(st) - 0.5;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offs = vec2(float(x), float(y));
            vec2 cellId = id + offs;
            float r = hash(cellId + seed);
            vec2 r2 = hash2(cellId + seed * 7.77);

            if (r < 0.25) continue;

            vec2 p = (r2 - 0.5) * 0.8;
            p += vec2(sin(t * 0.25 + r * 6.28) * 0.18,
                      cos(t * 0.20 + r * 6.28) * 0.18);

            float d = length(gv - (offs + p));

            float size = mix(minSize, maxSize, hash(cellId + seed + 5.5));
            size *= 1.0 + audioPulse * 0.25;

            float disc = smoothstep(size, size - size * blur, d);

            float opacity = 0.35 + 0.65 * hash(cellId + seed + 9.1);
            float twinkle = 0.75 + 0.25 * sin(t * (0.6 + r) + r * 40.0);

            acc += disc * opacity * twinkle;
            brightnessOut = max(brightnessOut, disc * opacity);
        }
    }
    return acc;
}

// ============================================================
//  SPARKLE DUST
// ============================================================
float sparkleLayer(vec2 uv, float density, float size, float seed,
                   float t, float treble) {
    float acc = 0.0;

    vec2 st = uv * density;
    st.y += t * 0.06;

    vec2 id = floor(st);
    vec2 gv = fract(st) - 0.5;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offs = vec2(float(x), float(y));
            vec2 cellId = id + offs;
            float r = hash(cellId + seed);

            if (r < 0.55) continue;

            vec2 p = (hash2(cellId + seed + 3.3) - 0.5) * 0.9;
            float d = length(gv - (offs + p));

            float tw = sin(t * (2.0 + r * 4.0) + r * 80.0);
            float twinkle = smoothstep(0.2, 1.0, tw);
            twinkle = twinkle * twinkle;
            twinkle *= 0.6 + treble * 0.8;

            float dot_ = smoothstep(size, size * 0.3, d);
            acc += dot_ * twinkle;
        }
    }
    return acc;
}

// ============================================================
//  MAIN
// ============================================================
void main() {
    vec2 uv = vUV;
    float aspect = iResolution.x / iResolution.y;
    vec2 suv = uv;
    vec2 p = uv;
    p.x *= aspect;

    // Audio
    float bass = clamp(iBass, 0.0, 1.0);
    float mid = clamp(iMid, 0.0, 1.0);
    float treble = clamp(iTreble, 0.0, 1.0);
    float beatPulse = smoothstep(1.0, 0.0, iBeat);

    float beatId = floor(iSongPosBeats);
    float beatFrac = smoothstep(0.0, 1.0, fract(iSongPosBeats));
    vec2 driftA = (hash2(vec2(beatId, 0.7)) - 0.5);
    vec2 driftB = (hash2(vec2(beatId + 1.0, 0.7)) - 0.5);
    vec2 musicDrift = mix(driftA, driftB, beatFrac) * (0.02 + bass * 0.06);

    // Background gradient
    vec2 diagDir = normalize(vec2(0.6, 0.8));
    float diag = clamp(dot(suv - vec2(0.05, 0.0), diagDir), 0.0, 1.0);
    diag = pow(diag, 1.4);

    vec3 darkAmber = vec3(0.13, 0.08, 0.02);
    vec3 midGold = vec3(0.45, 0.32, 0.06);
    vec3 brightGold = vec3(0.95, 0.83, 0.18);

    vec3 col = mix(darkAmber, midGold, diag);
    vec2 glowDir = normalize(vec2(0.55, 0.85));
    float glowZone = pow(clamp(dot(suv - vec2(0.25, 0.15), glowDir), 0.0, 1.0), 3.0);
    col = mix(col, brightGold, glowZone * (0.55 + bass * 0.15 + beatPulse * 0.1));

    col *= 1.0 + 0.05 * sin(suv.x * 9.0 + iTime * 0.2);

    // Bokeh layers
    float b;
    vec3 bokehWarm = vec3(1.0, 0.78, 0.22);

    float L1 = bokehLayer(p + musicDrift * 0.3, 2.2, 0.28, 0.42, 0.55, 0.020, 11.0, iTime, mid, b);
    col += bokehWarm * L1 * 0.14;

    float L2 = bokehLayer(p + musicDrift * 0.5, 3.4, 0.18, 0.30, 0.45, 0.035, 23.0, iTime, mid, b);
    col += bokehWarm * L2 * 0.18;

    vec3 discGold = vec3(1.0, 0.85, 0.30);
    float L3 = bokehLayer(p + musicDrift * 0.8, 5.5, 0.10, 0.20, 0.30, 0.050, 37.0, iTime, bass, b);
    col += discGold * L3 * 0.30;

    float L4 = bokehLayer(p + musicDrift, 8.0, 0.06, 0.12, 0.20, 0.070, 53.0, iTime, bass, b);
    col += discGold * L4 * 0.40;

    vec3 dotBright = vec3(1.0, 0.95, 0.55);
    float L5 = bokehLayer(p + musicDrift * 1.3, 13.0, 0.030, 0.060, 0.30, 0.090, 71.0, iTime, bass * 0.5, b);
    col += dotBright * L5 * 0.55;

    // Sparkle dust
    vec3 sparkCol = vec3(1.0, 1.0, 0.80);
    float S1 = sparkleLayer(p + musicDrift * 1.5, 22.0, 0.030, 91.0, iTime, treble);
    col += sparkCol * S1 * 0.50;

    float S2 = sparkleLayer(p + musicDrift * 1.8, 38.0, 0.020, 113.0, iTime, treble);
    col += sparkCol * S2 * 0.35;

    // Finishing
    col += brightGold * glowZone * glowZone * 0.12;
    float vig = 1.0 - 0.25 * pow(1.0 - diag, 2.0);
    col *= vig;

    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(0.92, 0.92, 0.92));
    col *= 1.08;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
