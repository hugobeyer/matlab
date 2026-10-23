#version 300 es

#ifndef SKIN_STAGE
#define SKIN_STAGE 3
#endif

/*
STAGES
------
0 = Fullscreen vertex shader for passes 1–5
1 = Tissue / fat / advected coordinates
2 = Tissue curvature and compression
3 = Layered height and roughness
4 = Directional strain
5 = Mean reduction
6 = Displaced mesh vertex shader
7 = Surface fragment shader

PASS ORDER AND BINDINGS
----------------------
1 -> tissueRaw, 1024 x 1024

2 -> tissueState, 1024 x 1024
     tissueMap = tissueRaw

3 -> heightRaw, 2048 x 2048
     tissueMap = tissueRaw
     stressMap = tissueState

4 -> heightFinal, 2048 x 2048
     source    = heightRaw
     stressMap = tissueState

5 -> reduction target at half the source dimensions
     source = heightFinal, then the preceding reduction
     Repeat until 1 x 1.

6 + 7 -> visible mesh
         heightMap  = heightFinal
         heightMean = final 1 x 1 reduction

Use floating-point RGBA targets, nearest filtering, no mipmaps,
no blending, and no color-space conversion for passes 1–5.
Set the viewport to the destination dimensions for every pass.
In WebGL 2, enable EXT_color_buffer_float for these render targets.
Stage 7 already applies ACES, sRGB encoding and 8-bit output dithering;
do not apply tone mapping or sRGB encoding again on the host.

COMPILE-TIME OPTIONS
--------------------
Insert stage/option defines after #version and before the shader body.
Define SKIN_STRAIN_ALIGN_FOLDS for stage 4 to align its base strain axis
with the stage-3 fold coordinate. Leave undefined for the original axis.
SKIN_CAVITY_AO defaults to 0.5 in stage 7; define it as 0.0 to disable.

HEIGHT OUTPUT
-------------
R = signed height
G = base roughness
B = fine-height residual
A = pore field

Mesh UVs must span [0,1]. The current mesh has 2048 x 2048 vertices.

DEFAULT UNIFORMS — SET THESE FROM THE HOST
----------------------------------------
seed               = 3
mapSize            = 2048
tissueSize         = 1024
planeSize          = 3.4

frequency          = 1
tissueFrequency    = 6
marbleDrift        = 1.15
tissueCoupling     = 0.9
cellStructure      = 0.12

curlFrequency      = 256
curlAmount         = 0
boxiness           = 0.85
cornerround        = 0.02

direction          = -0.18
elongation         = 3.8
foldWidth          = 0.14
crossing           = 0.65

fullness           = 0.9
compression        = 0.75
tension            = 0.15
softness           = 0.55
profile            = 0
relief             = 1

broadFrequency     = 16
mediumFrequency    = 96
microFrequency     = 240

broadStrength      = 0.85
mediumStrength     = 1
microStrength      = 1

grain              = 0.5
pores              = 0.65
rough              = 0.46
wavelengthScaling  = 0

cavityRough        = 0.12
curvatureRough     = 0.08
toneMappingExposure = 1

geometryOnly       = true
clay               = false

keyDirection = vec3(cos(2.4) * 3.0, 1.4, sin(2.4) * 3.0)

Supply modelMatrix, viewMatrix, projectionMatrix and cameraPosition.
Stage 7 also needs modelMatrix when geometryOnly is false. Its analytic
normal transform supports rotation and uniform scale only; nonuniform
scale or shear requires an inverse-transpose world-space normal matrix.
*/

precision highp float;
precision highp int;
precision highp sampler2D;

// -----------------------------------------------------------------------------
// Uniforms
// -----------------------------------------------------------------------------

uniform float seed;

uniform float mapSize;
uniform float tissueSize;
uniform float planeSize;

uniform float frequency;
uniform float tissueFrequency;
uniform float marbleDrift;
uniform float tissueCoupling;
uniform float cellStructure;

uniform float curlFrequency;
uniform float curlAmount;
uniform float boxiness;
uniform float cornerround;

uniform float direction;
uniform float elongation;
uniform float foldWidth;
uniform float crossing;

uniform float fullness;
uniform float compression;
uniform float tension;
uniform float softness;
uniform float profile;
uniform float relief;

uniform float broadFrequency;
uniform float mediumFrequency;
uniform float microFrequency;

uniform float broadStrength;
uniform float mediumStrength;
uniform float microStrength;

uniform float grain;
uniform float pores;
uniform float rough;
uniform float wavelengthScaling;

uniform float cavityRough;
uniform float curvatureRough;
uniform float toneMappingExposure;

uniform sampler2D tissueMap;
uniform sampler2D stressMap;
uniform sampler2D source;
uniform sampler2D heightMap;
uniform sampler2D heightMean;

// -----------------------------------------------------------------------------
// Shared functions
// -----------------------------------------------------------------------------

uint hashU(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;

    return x;
}

vec2 hash2(ivec2 p)
{
    uint h =
        hashU(uint(p.x)) ^
        hashU(uint(p.y) + 0x9e3779b9u) ^
        hashU(uint(int(seed)));

    return vec2(
        hashU(h ^ 0x68bc21ebu) >> 8,
        hashU(h ^ 0x02e5be93u) >> 8
    ) * (1.0 / 16777216.0);
}

float noise(vec2 p)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);

    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    return mix(
        mix(
            hash2(i).x,
            hash2(i + ivec2(1, 0)).x,
            f.x
        ),
        mix(
            hash2(i + ivec2(0, 1)).x,
            hash2(i + ivec2(1, 1)).x,
            f.x
        ),
        f.y
    );
}

mat2 rotation(float a)
{
    float c = cos(a);
    float s = sin(a);

    return mat2(c, -s, s, c);
}

float fractal(vec2 p)
{
    float result = 0.0;
    float weight = 0.5333333;

    for (int octave = 0; octave < 4; ++octave)
    {
        result += weight * (2.0 * noise(p) - 1.0);

        p =
            mat2(0.8, -0.6, 0.6, 0.8) * p * 2.031 +
            vec2(17.13, 39.71);

        weight *= 0.5;
    }

    return result;
}

vec2 driftVelocity(vec2 p)
{
    vec2 a = vec2(
        noise(p + 13.7),
        noise(p + vec2(79.4, 27.8))
    ) * 2.0 - 1.0;

    vec2 b = vec2(
        noise(p * 2.07 + vec2(31.2, 9.5)),
        noise(p * 2.07 + vec2(5.8, 57.1))
    ) * 2.0 - 1.0;

    return 0.74 * a + 0.26 * b;
}

// Bilinear sampling of a nearest-filtered float texture.
// UV endpoints correspond exactly to the first and last texels.
vec4 gridSample(sampler2D fieldMap, vec2 uv, float size)
{
    vec2 p = clamp(uv, 0.0, 1.0) * (size - 1.0);

    ivec2 a = ivec2(floor(p));
    ivec2 last = ivec2(int(size) - 1);

    vec2 f = fract(p);

    vec4 bottom = mix(
        texelFetch(fieldMap, a, 0),
        texelFetch(fieldMap, min(a + ivec2(1, 0), last), 0),
        f.x
    );

    vec4 top = mix(
        texelFetch(fieldMap, min(a + ivec2(0, 1), last), 0),
        texelFetch(fieldMap, min(a + ivec2(1, 1), last), 0),
        f.x
    );

    return mix(bottom, top, f.y);
}

// =============================================================================
// STAGE 0 — Fullscreen vertex shader
// =============================================================================

#if SKIN_STAGE == 0

in vec3 position;
in vec2 uv;

out vec2 vUv;

void main()
{
    vUv = uv;
    gl_Position = vec4(position.xy, 0.0, 1.0);
}

// =============================================================================
// STAGE 1 — Local tissue and advected coordinates
// =============================================================================

#elif SKIN_STAGE == 1

in vec2 vUv;
layout(location = 0) out vec4 outField;

// Compact, overlapping fat lobes with random centers, axes and amplitudes.
float fatLobes(vec2 p)
{
    ivec2 cell = ivec2(floor(p));
    vec2 local = fract(p);

    float height = 0.0;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 offset = ivec2(i, j);

            vec2 rnd = hash2(cell + offset);
            vec2 shape = hash2(cell + offset + ivec2(871, 167));

            vec2 d =
                local -
                vec2(offset) -
                (0.12 + 0.76 * rnd);

            d = rotation(shape.x * 6.2831853) * d;

            d /= vec2(
                0.65 + 0.32 * shape.y,
                0.43 + 0.32 * rnd.y
            );

            float t = max(0.0, 1.0 - dot(d, d));
            float lobe = t * t * t;

            lobe = mix(
                lobe,
                log(1.0 + 3.0 * lobe) / log(4.0),
                profile
            );

            height +=
                lobe *
                (0.38 + 0.86 * shape.x) *
                (0.45 + 0.55 * smoothstep(0.12, 0.65, rnd.x));
        }
    }

    return height;
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5) / (tissueSize - 1.0);

    float scale = max(tissueFrequency, 0.001);
    vec2 p = (uv - 0.5) * frequency * scale;

    // Midpoint integration through a seeded multiscale drift field.
    float stepSize = 0.34 * marbleDrift;

    for (int stepIndex = 0; stepIndex < 5; ++stepIndex)
    {
        vec2 velocity = driftVelocity(
            p * 0.43 + vec2(3.4, 12.7)
        );

        p += stepSize * driftVelocity(
            (p + 0.5 * stepSize * velocity) * 0.43 +
            vec2(3.4, 12.7)
        );
    }

    p += 0.11 * marbleDrift * vec2(
        fractal(p * 0.93 + 7.1),
        fractal(p * 0.93 + vec2(51.7, 16.3))
    );

    float footprint = max(length(dFdx(p)), length(dFdy(p)));
    float resolved = 1.0 - smoothstep(0.25, 0.65, footprint);

    float fat =
        0.78 * fatLobes(p + vec2(11.8, 7.3)) +
        0.22 * fatLobes(
            rotation(0.73) * p * 1.83 +
            vec2(41.1, 93.7)
        );

    float thickness = 0.6 + 0.4 * noise(p * 0.61 + 36.7);

    // R: tissue height
    // G,B: advected coordinates
    // A: local thickness variation
    outField = vec4(
        0.065 * fullness * fat * resolved,
        p / scale,
        thickness
    );
}

// =============================================================================
// STAGE 2 — Tissue curvature, compression and crown stretching
// =============================================================================

#elif SKIN_STAGE == 2

in vec2 vUv;
layout(location = 0) out vec4 outField;

float support(vec2 uv)
{
    return gridSample(tissueMap, uv, tissueSize).r;
}

void main()
{
    float rate = max(tissueFrequency * frequency, 0.001);

    float e = min(
        0.2,
        max(2.0 / (tissueSize - 1.0), 0.045 / rate)
    );

    vec2 uv = clamp(
        (gl_FragCoord.xy - 0.5) / (tissueSize - 1.0),
        vec2(e),
        vec2(1.0 - e)
    );

    float center = support(uv);

    float left  = support(uv - vec2(e, 0.0));
    float right = support(uv + vec2(e, 0.0));
    float down  = support(uv - vec2(0.0, e));
    float up    = support(uv + vec2(0.0, e));

    vec2 slope = vec2(right - left, up - down) /
        (2.0 * e * max(0.065 * rate, 0.0001));

    float curvatureScale =
        e * e * max(0.065 * rate * rate, 0.0001);

    float xx = (right + left - 2.0 * center) / curvatureScale;
    float yy = (up + down - 2.0 * center) / curvatureScale;

    float xy = (
        support(uv + vec2( e,  e)) -
        support(uv + vec2( e, -e)) -
        support(uv + vec2(-e,  e)) +
        support(uv - vec2( e,  e))
    ) / (4.0 * curvatureScale);

    float largest =
        0.5 * (xx + yy) +
        sqrt(0.25 * (xx - yy) * (xx - yy) + xy * xy);

    float gathered = smoothstep(0.12, 2.8, largest);
    float stretched = smoothstep(0.1, 2.0, -largest);

    // R,G: normalized tissue slope
    // B: compression proxy
    // A: convex-crown stretching proxy
    outField = vec4(slope, gathered, stretched);
}

// =============================================================================
// STAGE 3 — Layered height, pores and roughness
// =============================================================================

#elif SKIN_STAGE == 3

in vec2 vUv;
layout(location = 0) out vec4 outField;

// Rotated gradient of a nearest-site rounded box / Euclidean potential.
vec2 cellCurl(vec2 p)
{
    ivec2 base = ivec2(floor(p));
    vec2 local = fract(p);

    vec2 gradient = vec2(0.0);
    float best = 1e20;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 offset = ivec2(i, j);

            vec2 site =
                vec2(offset) + 0.5 +
                0.45 * 0.85 *
                sin(6.2831853 * hash2(base + offset));

            vec2 d = local - site;

            float a = d.x * d.x;
            float b = d.y * d.y;
            float difference = a - b;

            float root = sqrt(
                difference * difference +
                cornerround * cornerround +
                1e-12
            );

            float potential = mix(
                a + b,
                0.5 * (a + b + root),
                boxiness
            );

            if (potential < best)
            {
                best = potential;

                float selector = difference / root;

                gradient = mix(
                    2.0 * d,
                    vec2(
                        d.x * (1.0 + selector),
                        d.y * (1.0 - selector)
                    ),
                    boxiness
                );
            }
        }
    }

    return vec2(gradient.y, -gradient.x);
}

vec2 euclidean(vec2 p)
{
    ivec2 base = ivec2(floor(p));
    vec2 local = fract(p);

    float f1 = 1e20;
    float f2 = 1e20;

    for (int j = -2; j <= 2; ++j)
    {
        for (int i = -2; i <= 2; ++i)
        {
            ivec2 offset = ivec2(i, j);

            vec2 d =
                local -
                vec2(offset) -
                (0.12 + 0.76 * hash2(base + offset));

            float dd = dot(d, d);

            if (dd < f1)
            {
                f2 = f1;
                f1 = dd;
            }
            else if (dd < f2)
            {
                f2 = dd;
            }
        }
    }

    return sqrt(vec2(f1, f2));
}

float paperProfile(float x)
{
    x = clamp(x, 0.0, 3.0);

    return (x - 3.0) * (x - 3.0) * (x - 1.0) / 9.0;
}

float roundGap(float x)
{
    x = clamp(x / 0.55, 0.0, 1.0);

    float squareRootProfile =
        (sqrt(x + 0.04) - 0.2) /
        (sqrt(1.04) - 0.2);

    float logarithmicProfile =
        log(1.0 + 7.0 * x) / log(8.0);

    return mix(squareRootProfile, logarithmicProfile, profile);
}

vec3 layer(vec2 p, float width)
{
    vec2 f = euclidean(p);
    float gap = max(f.y - f.x, 0.0);

    float footprint = max(length(dFdx(p)), length(dFdy(p)));
    float visible = 1.0 - smoothstep(0.28, 0.72, footprint);

    float rounding = 0.003 + 0.035 * softness;
    float d = sqrt(gap * gap + rounding * rounding) - rounding;

    float w = max(width, 1.1 * footprint);
    float crossSection = paperProfile(d / max(w, 0.004));
    float cushion = roundGap(d);

    float h =
        (0.35 + 0.65 * compression) * min(crossSection, 0.0) +
        fullness * (
            2.1 * max(crossSection, 0.0) +
            0.45 * (cushion - 0.55)
        );

    return vec3(
        h * visible,
        exp(-d / max(w, 0.004)),
        visible
    );
}

// Independently wandering, tapered, irregularly spaced crease paths.
vec3 marbleLayer(vec2 p, float width, float salt, float load)
{
    float phase = p.x + marbleDrift * (
        0.84 * fractal(p * vec2(0.13, 0.075) + salt) +
        0.16 * (2.0 * noise(p * 0.39 + salt) - 1.0)
    );

    float footprint = max(length(dFdx(p)), length(dFdy(p)));

    float phaseFootprint = length(vec2(
        dFdx(phase),
        dFdy(phase)
    ));

    float visible = 1.0 - smoothstep(
        0.27,
        0.72,
        max(footprint, phaseFootprint)
    );

    float height = 0.0;
    float cavity = 0.0;

    int base = int(floor(phase));

    for (int i = -2; i <= 2; ++i)
    {
        int id = base + i;
        vec2 rnd = hash2(ivec2(id, int(salt)));

        float walk =
            0.42 * (
                2.0 * noise(vec2(
                    p.y * 0.12 + salt,
                    float(id) * 1.71
                )) - 1.0
            ) +
            0.15 * (
                2.0 * noise(vec2(
                    p.y * 0.37 + salt + 17.0,
                    float(id) * 2.93
                )) - 1.0
            );

        float line =
            float(id) + 0.5 +
            0.56 * (rnd.x - 0.5) +
            (marbleDrift / (1.0 + marbleDrift)) * walk;

        float delta = abs(phase - line);
        float rounding = 0.004 + 0.012 * softness;

        float distanceToLine =
            sqrt(delta * delta + rounding * rounding) -
            rounding;

        float taper = smoothstep(
            0.18,
            0.73,
            noise(vec2(
                p.y * 0.16 + rnd.x * 23.0,
                float(id) * 3.17 + salt
            ))
        );

        float w = min(
            0.5,
            max(
                width *
                (0.58 + 0.74 * rnd.y) *
                (0.7 + 0.3 * taper),
                0.85 * phaseFootprint
            )
        );

        float x = distanceToLine / max(w, 0.003);

        float section = paperProfile(x);
        float shoulder = roundGap(max(section, 0.0) * 2.8);

        float alive =
            (0.16 + 0.84 * taper) *
            (0.55 + 0.7 * rnd.x);

        float localHeight =
            2.0 * (0.28 + 0.72 * load) * min(section, 0.0) +
            0.42 * fullness * shoulder;

        height += localHeight * alive;
        cavity = max(cavity, exp(-x * x * 1.5) * alive);
    }

    return vec3(
        height * visible,
        cavity * visible,
        visible
    );
}

vec3 combinedLayer(vec2 p, float width, float salt, float load)
{
    vec3 walking = marbleLayer(p, width, salt, load);

    if (cellStructure > 0.0)
    {
        walking = mix(
            walking,
            layer(p + vec2(salt, 19.3), width),
            cellStructure
        );
    }

    return walking;
}

float poreField(vec2 p)
{
    ivec2 base = ivec2(floor(p));
    vec2 local = fract(p);

    float sum = 0.0;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            ivec2 offset = ivec2(i, j);

            vec2 h = hash2(base + offset + ivec2(93));

            vec2 d =
                local -
                vec2(offset) -
                (0.12 + 0.76 * hash2(base + offset));

            d = rotation(h.y * 6.2831853) * d;

            float radius = 0.1 + 0.12 * h.x;

            d /= vec2(
                radius,
                radius * (0.6 + 0.35 * h.y)
            );

            float r = length(d);

            float bowl = exp(-r * r * 1.8);
            float rr = (r - 1.2) / 0.4;
            float rim = exp(-rr * rr);

            sum +=
                (bowl - 0.15 * rim) *
                (0.5 + 0.5 * h.x);
        }
    }

    return sum;
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5) / (mapSize - 1.0);

    vec4 tissue = gridSample(tissueMap, uv, tissueSize);
    vec4 stress = gridSample(stressMap, uv, tissueSize);

    vec2 curlUV = vec2(0.0);

    if (curlAmount != 0.0)
    {
        curlUV =
            (curlAmount / max(curlFrequency, 1.0)) *
            (
                cellCurl(uv * curlFrequency) +
                0.23 * cellCurl(
                    uv * curlFrequency * 2.03 + 13.7
                )
            );
    }

    vec2 tissueOffset =
        0.085 * tissueCoupling /
        max(tissueFrequency, 0.001) *
        stress.xy /
        (1.0 + length(stress.xy));

    vec2 q = rotation(direction) * (
        tissue.gb +
        tissueOffset +
        curlUV * frequency
    );

    float stretch = sqrt(max(1.0, elongation));

    float localLoad = compression * mix(
        1.0,
        0.22 + 1.55 * stress.z,
        tissueCoupling
    );

    float crownRelax =
        1.0 -
        0.8 * tissueCoupling * stress.w *
        (0.4 + 0.6 * tension);

    vec2 bq =
        q * vec2(1.0 / stretch, stretch) * broadFrequency +
        vec2(11.7, 19.2);

    vec3 b = combinedLayer(
        bq,
        foldWidth,
        41.0,
        localLoad
    );

    vec2 mq =
        rotation(0.13 * crossing) *
        q *
        vec2(1.0 / sqrt(stretch), sqrt(stretch)) *
        mediumFrequency +
        vec2(43.1, 12.8);

    vec3 m = combinedLayer(
        mq,
        foldWidth * 0.72,
        137.0,
        localLoad +
        0.5 * b.y * broadStrength * tissueCoupling
    );

    vec2 sq =
        rotation(crossing * 1.17 + 0.15) *
        q * microFrequency +
        vec2(89.3, 57.1);

    vec3 s = combinedLayer(
        sq,
        foldWidth * 0.5,
        283.0,
        localLoad +
        0.35 * m.y * mediumStrength * tissueCoupling
    );

    vec3 rate = max(
        vec3(
            broadFrequency,
            mediumFrequency,
            microFrequency
        ) * frequency,
        vec3(1.0)
    );

    vec3 amplitude =
        vec3(0.027, 0.007, 0.0017) *
        vec3(
            broadStrength,
            mediumStrength,
            microStrength
        );

    amplitude *= mix(
        vec3(1.0),
        vec3(16.0, 96.0, 240.0) / rate,
        wavelengthScaling
    );

    float patchDepth = 0.65 + 0.65 * tissue.a;

    float mediumGain = crownRelax * mix(
        1.0,
        0.3 + 1.35 * stress.z + 0.55 * b.y * broadStrength,
        tissueCoupling
    );

    float microGain =
        (0.5 + 0.5 * crossing) *
        crownRelax *
        mix(
            1.0,
            0.42 + 1.1 * stress.z + 0.5 * m.y * mediumStrength,
            tissueCoupling
        );

    float fatHeight = tissue.r * mix(
        1.0,
        1.0 / max(frequency, 0.001),
        wavelengthScaling
    );

    float base = fatHeight + patchDepth * (
        amplitude.x * b.x +
        amplitude.y * m.x * mediumGain
    );

    float fine = amplitude.z * s.x * microGain;

    float grainFootprint = max(
        length(dFdx(q * 350.0)),
        length(dFdy(q * 350.0))
    );

    fine +=
        0.00032 * grain *
        (noise(q * 350.0) - 0.5) *
        (1.0 - smoothstep(0.3, 0.75, grainFootprint));

    float poreFootprint = max(
        length(dFdx(q * 125.0)),
        length(dFdy(q * 125.0))
    );

    float poreVisible =
        1.0 - smoothstep(0.25, 0.65, poreFootprint);

    float pore =
        poreField(q * 125.0 + 71.0) *
        poreVisible;

    fine -=
        0.001 * pores * pore *
        (0.65 + 0.35 * crownRelax);

    float roughness = clamp(
        rough +
        0.035 * m.y +
        0.06 * max(0.0, pore) +
        0.025 * (1.0 - s.z) +
        0.045 * tissueCoupling * stress.z -
        0.04 * tissueCoupling * stress.w,
        0.1,
        0.95
    );

    outField = vec4(
        relief * (base + fine),
        roughness,
        relief * fine,
        pore
    );
}

// =============================================================================
// STAGE 4 — Directional strain of the fine-height residual
// =============================================================================

#elif SKIN_STAGE == 4

in vec2 vUv;
layout(location = 0) out vec4 outField;

float strainTap(vec2 pos)
{
    return gridSample(source, pos / (mapSize - 1.0), mapSize).b;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 center = texelFetch(source, p, 0);

    vec2 pos = vec2(p);
    vec2 uv = pos / (mapSize - 1.0);
    vec4 stress = gridSample(stressMap, uv, tissueSize);

#ifdef SKIN_STRAIN_ALIGN_FOLDS
    vec2 axis = vec2(cos(direction), sin(direction));
#else
    vec2 axis = vec2(cos(direction), -sin(direction));
#endif

    vec2 tangent =
        vec2(-stress.y, stress.x) /
        (length(stress.xy) + 0.00001);

    if (dot(tangent, axis) < 0.0)
    {
        tangent = -tangent;
    }

    axis = normalize(axis + 0.8 * tissueCoupling * tangent);

    float blur = center.b;
    float weight = 1.0;

    for (int k = 1; k <= 3; ++k)
    {
        float w = exp(-float(k * k) / 3.0);
        vec2 o = axis * float(k);

        blur += w * (strainTap(pos + o) + strainTap(pos - o));
        weight += 2.0 * w;
    }

    vec2 crossAxis = vec2(-axis.y, axis.x) * 1.5;

    float low =
        0.5 * center.b +
        0.25 * (
            strainTap(pos + crossAxis) +
            strainTap(pos - crossAxis)
        );

    float localTension = clamp(
        tension + 0.55 * tissueCoupling * stress.w,
        0.0,
        1.0
    );

    float localCompression = compression * mix(
        1.0,
        0.25 + 0.75 * stress.z,
        tissueCoupling
    );

    float detail =
        mix(center.b, blur / weight, localTension) +
        0.35 * localCompression * (center.b - low);

    outField = vec4(
        center.r - center.b + detail,
        center.g,
        detail,
        center.a
    );
}

// =============================================================================
// STAGE 5 — Mean reduction
// =============================================================================

#elif SKIN_STAGE == 5

in vec2 vUv;
layout(location = 0) out vec4 outField;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;

    outField = 0.25 * (
        texelFetch(source, p, 0) +
        texelFetch(source, p + ivec2(1, 0), 0) +
        texelFetch(source, p + ivec2(0, 1), 0) +
        texelFetch(source, p + ivec2(1, 1), 0)
    );
}

// =============================================================================
// STAGE 6 — Actual mesh vertex displacement
// =============================================================================

#elif SKIN_STAGE == 6

in vec2 uv;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

out vec2 surfaceUV;
out vec3 worldPosition;

void main()
{
    surfaceUV = uv;

    ivec2 pixel = ivec2(round(uv * (mapSize - 1.0)));

    float height =
        texelFetch(heightMap, pixel, 0).r -
        texelFetch(heightMean, ivec2(0), 0).r;

    // The generated height is the vertex's actual Y position.
    vec3 displaced = vec3(
        (uv.x - 0.5) * planeSize,
        height,
        (0.5 - uv.y) * planeSize
    );

    vec4 world = modelMatrix * vec4(displaced, 1.0);

    worldPosition = world.xyz;
    gl_Position = projectionMatrix * viewMatrix * world;
}

// =============================================================================
// STAGE 7 — Surface lighting, cavity and curvature roughness
// =============================================================================

#elif SKIN_STAGE == 7

in vec2 surfaceUV;
in vec3 worldPosition;

layout(location = 0) out vec4 pixelColor;

uniform mat4 modelMatrix;
uniform vec3 cameraPosition;
uniform vec3 keyDirection;

uniform bool geometryOnly;
uniform bool clay;

#ifndef SKIN_CAVITY_AO
#define SKIN_CAVITY_AO 0.5
#endif

vec4 sampleField(vec2 uv)
{
    return gridSample(heightMap, uv, mapSize);
}

vec3 lightBRDF(
    vec3 albedo,
    vec3 N,
    vec3 V,
    vec3 L,
    float roughness
)
{
    vec3 H = normalize(V + L);

    float nl = max(dot(N, L), 0.0);
    float nv = max(dot(N, V), 0.001);
    float nh = max(dot(N, H), 0.0);
    float vh = max(dot(V, H), 0.0);

    float a = max(0.045, roughness * roughness);
    float a2 = a * a;

    float dn = nh * nh * (a2 - 1.0) + 1.0;
    float d = a2 / (3.14159265 * dn * dn);

    float k = pow(roughness + 1.0, 2.0) / 8.0;

    float g =
        (nv / (nv * (1.0 - k) + k)) *
        (nl / (nl * (1.0 - k) + k));

    vec3 f =
        vec3(0.035) +
        (1.0 - 0.035) * pow(1.0 - vh, 5.0);

    // The outer nl cancels the specular denominator's nl; g retains it.
    return nl * (1.0 - f) * albedo / 3.14159265 +
        d * g * f / (4.0 * nv);
}

// Expanded equivalents of the Three.js ACES and sRGB shader chunks.
vec3 RRTAndODTFit(vec3 v)
{
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;

    return a / b;
}

vec3 ACESFilmicToneMapping(vec3 color)
{
    const mat3 inputMatrix = mat3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777)
    );

    const mat3 outputMatrix = mat3(
        vec3( 1.60475, -0.10208, -0.00327),
        vec3(-0.53108,  1.10813, -0.07276),
        vec3(-0.07367, -0.00605,  1.07602)
    );

    color *= toneMappingExposure / 0.6;
    color = inputMatrix * color;
    color = RRTAndODTFit(color);
    color = outputMatrix * color;

    return clamp(color, 0.0, 1.0);
}

vec3 linearToSRGB(vec3 color)
{
    return mix(
        pow(color, vec3(0.41666)) * 1.055 - vec3(0.055),
        color * 12.92,
        vec3(lessThanEqual(color, vec3(0.0031308)))
    );
}

void main()
{
    vec4 field = sampleField(surfaceUV);
    float delta = 1.0 / (mapSize - 1.0);

    float left = sampleField(
        surfaceUV - vec2(delta, 0.0)
    ).r;

    float right = sampleField(
        surfaceUV + vec2(delta, 0.0)
    ).r;

    float down = sampleField(
        surfaceUV - vec2(0.0, delta)
    ).r;

    float up = sampleField(
        surfaceUV + vec2(0.0, delta)
    ).r;

    // Default shading normal comes from displaced geometry.
    vec3 N = normalize(cross(
        dFdx(worldPosition),
        dFdy(worldPosition)
    ));

    if (!geometryOnly)
    {
        // Rotation/uniform scale only; otherwise use an inverse-transpose matrix.
        N = normalize(mat3(modelMatrix) * normalize(vec3(
            -(right - left) / (2.0 * delta * planeSize),
            1.0,
            (up - down) / (2.0 * delta * planeSize)
        )));
    }

    vec3 V = normalize(cameraPosition - worldPosition);

    if (dot(N, V) < 0.0)
    {
        N = -N;
    }

    float reliefNorm = 1.0 / max(relief, 0.05);

    float curvature = (
        0.25 * (left + right + down + up) -
        field.r
    ) * reliefNorm;

    float convex =
        max(0.0, -curvature) /
        (0.00012 + abs(curvature));

    vec2 e = vec2(5.0 * delta, 0.0);

    float ring = 0.25 * (
        sampleField(surfaceUV + e).r +
        sampleField(surfaceUV - e).r +
        sampleField(surfaceUV + e.yx).r +
        sampleField(surfaceUV - e.yx).r
    );

    float ringDelta = (ring - field.r) * reliefNorm;
    float cavity =
        max(0.0, ringDelta) /
        (0.0015 + abs(ringDelta));

    float roughness = clamp(
        field.g +
        cavityRough * cavity -
        curvatureRough * convex,
        0.09,
        0.96
    );

    float mottling = noise(surfaceUV * 22.0 + 41.0);

    vec3 color = clay
        ? vec3(0.37)
        : vec3(0.46, 0.255, 0.18) *
          (0.94 + 0.12 * mottling);

    vec3 L = normalize(keyDirection);

    vec3 lit =
        lightBRDF(color, N, V, L, roughness) *
        vec3(4.2, 3.85, 3.5);

    lit += lightBRDF(
        color,
        N,
        V,
        normalize(vec3(-2.0, 1.0, -3.0)),
        roughness
    ) * vec3(0.5, 0.58, 0.7);

    lit += color * (0.19 + 0.1 * max(N.y, 0.0)) *
        (1.0 - SKIN_CAVITY_AO * cavity);

    vec3 srgb = linearToSRGB(ACESFilmicToneMapping(lit));
    float ign = fract(52.9829189 * fract(dot(
        gl_FragCoord.xy,
        vec2(0.06711056, 0.00583715)
    )));

    pixelColor = vec4(srgb + (ign - 0.5) / 255.0, 1.0);
}

#else
#error Invalid SKIN_STAGE
#endif
