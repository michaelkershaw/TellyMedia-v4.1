// ============================================================
//  GOLDEN GLITTER BOKEH  -  Luxury Edition
//  TellyMedia v4 Compatible
//  Matches reference: soft bokeh discs, bright dots, fine
//  sparkle dust over a diagonal amber-to-gold gradient.
// ============================================================

cbuffer ShaderCB : register(b0)
{
    float2 iResolution;
    float  iTime;
    float  iBeat;
    float  iLevel;
    float  iBass, iMid, iTreble;
    float  iBpm;
    float  iSongPosBeats;
    float  _pad;
    float panelX, panelY, panelW, panelH;
};

// ---------- Hash helpers ----------
float hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 hash2(float2 p)
{
    return float2(hash(p), hash(p + 57.31));
}

// ============================================================
//  BOKEH LAYER
//  Soft translucent discs that drift upward slowly.
//  blur:   edge softness (high = dreamy, low = crisp)
//  rise:   upward drift speed
// ============================================================
float bokehLayer(float2 uv, float density, float minSize, float maxSize,
                 float blur, float rise, float seed, float iTime,
                 float audioPulse, out float brightnessOut)
{
    brightnessOut = 0.0;
    float acc = 0.0;

    // Slow upward rise (like champagne bubbles) + tiny side sway
    float2 st = uv * density;
    st.y += iTime * rise;
    st.x += sin(iTime * 0.15 + seed * 3.0) * 0.15;

    float2 id = floor(st);
    float2 gv = frac(st) - 0.5;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 offs = float2(x, y);
            float2 cellId = id + offs;
            float  r  = hash(cellId + seed);
            float2 r2 = hash2(cellId + seed * 7.77);

            // Skip some cells for natural irregular spacing
            if (r < 0.25) continue;

            // Position inside cell + slow personal wobble
            float2 p = (r2 - 0.5) * 0.8;
            p += float2(sin(iTime * 0.25 + r * 6.28) * 0.18,
                        cos(iTime * 0.20 + r * 6.28) * 0.18);

            float d = length(gv - (offs + p));

            // Size varies per particle; gentle audio breathing
            float size = lerp(minSize, maxSize, hash(cellId + seed + 5.5));
            size *= 1.0 + audioPulse * 0.25;

            // Soft round disc
            float disc = smoothstep(size, size - size * blur, d);

            // Per-particle opacity variation (key to the bokeh look)
            float opacity = 0.35 + 0.65 * hash(cellId + seed + 9.1);

            // Slow twinkle (independent timing per disc)
            float twinkle = 0.75 + 0.25 * sin(iTime * (0.6 + r) + r * 40.0);

            acc += disc * opacity * twinkle;
            brightnessOut = max(brightnessOut, disc * opacity);
        }
    }
    return acc;
}

// ============================================================
//  SPARKLE DUST  -  tiny crisp glints with hard twinkle
// ============================================================
float sparkleLayer(float2 uv, float density, float size, float seed,
                   float iTime, float treble)
{
    float acc = 0.0;

    float2 st = uv * density;
    st.y += iTime * 0.06; // very slow rise

    float2 id = floor(st);
    float2 gv = frac(st) - 0.5;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 offs = float2(x, y);
            float2 cellId = id + offs;
            float  r = hash(cellId + seed);

            if (r < 0.55) continue; // sparse dust

            float2 p = (hash2(cellId + seed + 3.3) - 0.5) * 0.9;
            float d = length(gv - (offs + p));

            // Hard twinkle: sparkles pop in and out
            float tw = sin(iTime * (2.0 + r * 4.0) + r * 80.0);
            float twinkle = smoothstep(0.2, 1.0, tw);
            twinkle = twinkle * twinkle;

            // Treble makes the dust sparkle more aggressively
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
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float aspect = iResolution.x / iResolution.y;
    float2 suv = uv;          // screen uv  (0-1)
    float2 p = uv;
    p.x *= aspect;            // square units for round discs

    // ---------- Audio ----------
    float bass   = saturate(iBass);
    float mid    = saturate(iMid);
    float treble = saturate(iTreble);
    float beatPulse = smoothstep(1.0, 0.0, iBeat);

    // Smooth beat-driven drift: whole field breathes gently with music
    float beatId   = floor(iSongPosBeats);
    float beatFrac = smoothstep(0.0, 1.0, frac(iSongPosBeats));
    float2 driftA  = (hash2(float2(beatId, 0.7)) - 0.5);
    float2 driftB  = (hash2(float2(beatId + 1.0, 0.7)) - 0.5);
    float2 musicDrift = lerp(driftA, driftB, beatFrac) * (0.02 + bass * 0.06);

    // ============================================
    //  1. BACKGROUND - diagonal amber->gold gradient
    //     (dark top-left, glowing bottom-right)
    // ============================================
    float diag = saturate(dot(suv - float2(0.05, 0.0), normalize(float2(0.6, 0.8))));
    diag = pow(diag, 1.4);

    float3 darkAmber   = float3(0.13, 0.08, 0.02);
    float3 midGold     = float3(0.45, 0.32, 0.06);
    float3 brightGold  = float3(0.95, 0.83, 0.18);

    float3 col = lerp(darkAmber, midGold, diag);
    // Hot glow zone in lower-right like the reference
    float glowZone = pow(saturate(dot(suv - float2(0.25, 0.15), normalize(float2(0.55, 0.85)))), 3.0);
    col = lerp(col, brightGold, glowZone * (0.55 + bass * 0.15 + beatPulse * 0.1));

    // Very subtle vertical light shafts for richness
    col *= 1.0 + 0.05 * sin(suv.x * 9.0 + iTime * 0.2);

    // ============================================
    //  2. LARGE SOFT BOKEH  (background depth)
    // ============================================
    float b;
    float3 bokehWarm = float3(1.0, 0.78, 0.22);

    float L1 = bokehLayer(p + musicDrift * 0.3, 2.2, 0.28, 0.42, 0.55, 0.020, 11.0, iTime, mid, b);
    col += bokehWarm * L1 * 0.14;

    float L2 = bokehLayer(p + musicDrift * 0.5, 3.4, 0.18, 0.30, 0.45, 0.035, 23.0, iTime, mid, b);
    col += bokehWarm * L2 * 0.18;

    // ============================================
    //  3. MEDIUM BOKEH  (main visible discs)
    // ============================================
    float3 discGold = float3(1.0, 0.85, 0.30);

    float L3 = bokehLayer(p + musicDrift * 0.8, 5.5, 0.10, 0.20, 0.30, 0.050, 37.0, iTime, bass, b);
    col += discGold * L3 * 0.30;

    float L4 = bokehLayer(p + musicDrift, 8.0, 0.06, 0.12, 0.20, 0.070, 53.0, iTime, bass, b);
    col += discGold * L4 * 0.40;

    // ============================================
    //  4. SMALL BRIGHT DOTS  (crisp foreground)
    // ============================================
    float3 dotBright = float3(1.0, 0.95, 0.55);

    float L5 = bokehLayer(p + musicDrift * 1.3, 13.0, 0.030, 0.060, 0.30, 0.090, 71.0, iTime, bass * 0.5, b);
    col += dotBright * L5 * 0.55;

    // ============================================
    //  5. FINE SPARKLE DUST
    // ============================================
    float3 sparkCol = float3(1.0, 1.0, 0.80);

    float S1 = sparkleLayer(p + musicDrift * 1.5, 22.0, 0.030, 91.0, iTime, treble);
    col += sparkCol * S1 * 0.50;

    float S2 = sparkleLayer(p + musicDrift * 1.8, 38.0, 0.020, 113.0, iTime, treble);
    col += sparkCol * S2 * 0.35;

    // ============================================
    //  6. FINISHING
    // ============================================
    // Gentle bloom lift in the bright zone
    col += brightGold * glowZone * glowZone * 0.12;

    // Soft vignette top-left to deepen contrast
    float vig = 1.0 - 0.25 * pow(1.0 - diag, 2.0);
    col *= vig;

    // Tone & exposure
    col = saturate(col);
    col = pow(col, float3(0.92, 0.92, 0.92)); // slight lift for richness
    col *= 1.08;

    return float4(saturate(col), 1.0);
}
