// ============================================================================
// MIN CARVE - Copernicus OpenCL COP
// Erode a height by min-intersecting it with copies of itself. Tileable.
//
// Order: copies carve the input -> flow slumps.
//
//   copy       weight of the copy cuts (0-1)
//   copies     up to 2; each copy gets a random 90-degree turn / mirror,
//              offset, bias and broad noise patch.
//   seed       rerolls copies, patches and angle variation
//   angle      base flow direction in degrees, with seeded variation
//   dir        optional float2 layer; when non-zero it steers flow
//   sdf        optional exact crack distance; otherwise cavity depth is estimated
//   FLOW_AMOUNT, FLOW_DIST_MAX, FLOW_DIST_OFFSET, ANGLE_VARIATION and crevice
//   settings below are hardcoded script controls.
//
// Outputs: out (carved height), mask (amount removed).
// ============================================================================

#bind layer height float border=WRAP
#bind layer dir? float2 val=0 border=WRAP
#bind layer sdf? float val=1e9 border=WRAP
#bind layer !&out float
#bind layer !&mask float
#bind layer !&fmask float

#bind parm copy float val=0.6
#bind parm copies float val=2
#bind parm seed int val=0
#bind parm angle float val=-90

#define TAU 6.2831853f
#define MAXC 2
#define FLOW_AMOUNT 0.2f
#define FLOW_DIST_MAX 0.25f
#define FLOW_DIST_OFFSET 0.25f
#define ANGLE_VARIATION 30.0f
#define CREVICE_AMOUNT 0.1f
#define CREVICE_RADIUS 0.0f
#define CREVICE_BANDS 0.01f

float rnd(uint a, uint b)
{
    uint v = a * 747796405u + b * 2891336453u + 12345u;
    uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (float)(((w >> 22u) ^ w) >> 8) * (1.0f / 16777216.0f);
}

int wrapi(int i, int n) { return ((i % n) + n) % n; }

// Periodic value noise on uv, lattice F, 0..1.
float vnoise(float2 uv, int F, uint seed)
{
    float2 q = uv * (float)F;
    float2 fl = floor(q);
    float2 f = q - fl;
    f = f * f * (3.0f - 2.0f * f);
    int x0 = wrapi((int)fl.x, F), y0 = wrapi((int)fl.y, F);
    int x1 = wrapi(x0 + 1, F), y1 = wrapi(y0 + 1, F);
    float a = rnd(seed, (uint)(x0 + y0 * F));
    float b = rnd(seed, (uint)(x1 + y0 * F));
    float c = rnd(seed, (uint)(x0 + y1 * F));
    float d = rnd(seed, (uint)(x1 + y1 * F));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// One of the 8 square symmetries (turns + mirrors), about the tile centre.
float2 sym(float2 x, int t)
{
    float2 c = x - 0.5f;
    if (t & 4)
        c.x = -c.x;
    int r = t & 3;
    c = r == 0 ? c : (r == 1 ? (float2)(-c.y, c.x) : (r == 2 ? -c : (float2)(c.y, -c.x)));
    return c + 0.5f;
}

float carve(float h, float c, float w)
{
    return mix(h, fmin(h, c), clamp(w, 0.0f, 1.0f));
}

// Patch weight of a copy at x: 2-octave periodic noise, soft threshold.
float patch(float2 x, uint ps)
{
    float pm = 0.65f * vnoise(x, 3, ps) + 0.35f * vnoise(x, 6, ps + 1u);
    return smoothstep(0.45f, 0.65f, pm);
}

@KERNEL
{
    float2 uv = @P.texture;
    float wc = @copy;
    // Smoothly compress long travel distances instead of wrapping them with fmod.
    // This avoids abrupt distance resets and limits texture-space distortion.
    float fd_raw = fmax(FLOW_DIST_OFFSET, 0.0f);
    float fd_max = max(FLOW_DIST_MAX, 1e-4f);
    float fd = fd_max * (1.0f - exp(-fd_raw / fd_max));
    fd = max(fd, 1e-4f);

    // ---- per-copy setup ---------------------------------------------------
    int    T[MAXC];
    float2 OFF[MAXC];
    float  BIAS[MAXC], W[MAXC];
    uint   PS[MAXC];
    float cf = clamp(@copies, 0.0f, (float)MAXC);
    int n = wc > 0.0f ? (int)ceil(cf) : 0;
    uint sd = (uint)@seed * 7919u + 1u;

    for (int i = 0; i < n; i++)
    {
        uint b = (uint)i * 8u;
        T[i] = (int)(rnd(sd, b) * 8.0f) & 7;
        OFF[i] = (float2)(rnd(sd, b + 1u), rnd(sd, b + 2u));
        BIAS[i] = 0.03f + 0.12f * rnd(sd, b + 3u);
        W[i] = wc * fmin(1.0f, cf - (float)i);
        PS[i] = sd * 31u + b + 7u;
    }

    // ---- C at the pixel --------------------------------------------------
    float H = @height.textureSample(uv);
    float h = H;
    for (int i = 0; i < n; i++)
        h = carve(h, @height.textureSample(sym(uv, T[i]) + OFF[i]) + BIAS[i],
                  W[i] * patch(uv, PS[i]));

    float fm = 0.0f;

    // Gravity direction with slow spatial wobble and a seeded per-instance angle variation.
    float wob = (vnoise(uv, 2, sd + 501u) - 0.5f) * 1.05f;
    float angle_jitter = (rnd(sd, 502u) - 0.5f) * 2.0f * ANGLE_VARIATION;
    float ga = radians(@angle + angle_jitter) + wob;
    float2 dg = (float2)(cos(ga), sin(ga));

    // Own downhill gradient provides the secondary direction drive.
    float2 g = (float2)(0.0f);
    for (int j = 0; j < 8; j++)
    {
        float ang = TAU * (float)j / 8.0f;
        float2 o = (float2)(cos(ang), sin(ang));
        g += o * @height.textureSample(uv + o * fd);
    }
    float gl = length(g);
    float2 own = gl > 1e-6f ? -g / gl : dg;
    float2 mixed_direction = mix(dg, own, 0.25f);
    float ml = length(mixed_direction);
    float2 dn = ml > 1e-6f ? mixed_direction / ml : dg;

    // Optional external direction layer overrides the generated direction.
    float2 ext = @dir;
    float el = length(ext);
    if (el > 1e-4f)
        dn = ext / el;

    // Crevice feather from exact SDF when connected; otherwise estimate cavity depth.
    float cr = max(CREVICE_RADIUS, 1e-4f);
    float sdv = @sdf;
    float dist;
    if (sdv < 1e8f)
    {
        dist = fabs(sdv) / cr;
    }
    else
    {
        float ra = 0.0f;
        for (int j = 0; j < 8; j++)
        {
            float ang = TAU * ((float)j + 0.5f) / 8.0f;
            ra += @height.textureSample(uv + (float2)(cos(ang), sin(ang)) * cr);
        }
        float cav = ra / 8.0f - H;
        dist = 1.0f - smoothstep(0.0f, 0.12f, cav);
    }
    float near = 1.0f - smoothstep(0.0f, 1.0f, dist);

    // CREVICE_BANDS is spacing in normalized SDF-distance units.
    if (CREVICE_BANDS > 0.0f)
    {
        float band_phase = dist / CREVICE_BANDS;
        near *= 1.0f - (band_phase - floor(band_phase));
    }
    fm = mix(1.0f, near, CREVICE_AMOUNT);

    h = carve(h, @height.textureSample(uv + dn * fd) + 0.1f, FLOW_AMOUNT * fm);

    @out.set(h);
    @mask.set(H - h);
    @fmask.set(fm);
}
