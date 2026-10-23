#runover layer
#bind layer size_ref float port=size_ref
#bind layer src? val=0 float2 port=src
#bind layer &uvscale? float val=1 port=uvscale
#bind layer seed? val=0 int port=seed
#bind layer !&strata_coord float port=strata_coord
#bind layer !&strata_profile float port=strata_profile
#bind layer !&boundary_distance float port=boundary_distance
#bind layer !&flow float2 port=flow
#bind layer !&uvs float2 port=uvs
#bind layer !&uvs_b float2 port=uvs_b
#bind layer !&id int port=id
#bind layer !&height float port=height
#bind layer !&height_min float port=height_min
#bind layer !&height_max float port=height_max

#bind parm strata_count int val=8
#bind parm drift_cycles int val=0
#bind parm jagged_amount float val=0.09
#bind parm jagged_offset float2 val=0.19
#bind parm worley_cells int val=4
#bind parm worley_octaves int val=3
#bind parm persistence float val=0.28
#bind parm lacunarity float val=2.0
#bind parm jitter float val=0.75
#bind parm distance_random float val=0.22
#bind parm depth_random float val=0.55

#bind parm cliff_steepness float val=0.92
#bind parm plateau_width float val=0.68
#bind parm cliff_height_bias float val=0.85
#bind parm edge_sharpness float val=0.035
#bind parm worley_strength float val=0.55          // overall Worley influence (was too strong)
#bind parm branch_diff float val=0.75              // how different A vs B are
#bind parm eikonal_strength float val=0.85         // directional edge falloff
#bind parm feather_axis float val=0.65             // 0 = isotropic, 1 = strong XY+ / strata-aligned

#bind parm flow_strength float val=0.025
#bind parm flow_offset_a float2 val=0.173
#bind parm flow_offset_b float2 val=0.417

#bind parm pivot float2
#bind parm translate float2
#bind parm rotate float
#bind parm scale float2 val=1
#bind parm skew float2
#bind parm seed_parm int
#bind parm rand_translate float2
#bind parm rand_rotate float
#bind parm rand_scalemin float2 val=1
#bind parm rand_scalemax float2 val=1
#bind parm rand_scaleuniform int

#bind parm frequency_a int val=3
#bind parm frequency_b int val=7
#bind parm map_b_offset float2 val=0.271
#bind parm map_b_flow_scale float val=0.55

#bind parm cells int val=2
#bind parm detail float val=0.45
#bind parm irregularity float val=0.65
#bind parm strata_mix float val=0.72
#bind parm final_mode int val=0
#bind parm final_mix float val=0.5
#bind parm output_min float val=0.25
#bind parm output_max float val=1.35

#import <random.h>
#define ST_TAU 6.283185307179586f

float st_frac(float x) { return x - floor(x); }
float2 st_frac2(float2 x) { return x - floor(x); }
int st_wrapi(int x, int n) { return ((x % n) + n) % n; }

uint st_hash(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float st_rand(uint a, uint b, uint c)
{
    return (float)(st_hash(a ^ st_hash(b + st_hash(c))) >> 8) * (1.0f / 16777216.0f);
}

static float2 _dorotate(float2 uv, float rotate)
{
    float c = cospi(rotate / 180.0f);
    float s = sinpi(rotate / 180.0f);
    return (float2)(uv.x * c + uv.y * s, -uv.x * s + uv.y * c);
}

static float2 _doskew(float2 v, float2 skew)
{
    float2 src = v;
    v.x = src.x + src.y * skew.x;
    v.y = src.y + src.x * skew.y;
    return v;
}

float st_worley_f2f1(float2 uv, int cells, float jitter, float distance_random, float depth_random, uint seed)
{
    int ncells = max(cells, 1);
    float2 p = st_frac2(uv) * (float)ncells;
    int2 base = (int2)((int)floor(p.x), (int)floor(p.y));
    float f1 = 1e10f, f2 = 1e10f;
    float winner_depth = 1.0f;

    for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++)
    {
        int gx = base.x + ox, gy = base.y + oy;
        int wx = st_wrapi(gx, ncells), wy = st_wrapi(gy, ncells);
        uint cid = st_hash(seed ^ ((uint)(wx + wy * ncells) * 0x9E3779B9u));

        float2 j = (float2)(st_rand(cid, 11u, 97u), st_rand(cid, 12u, 97u));
        j = (j - 0.5f) * jitter;
        float2 site = (float2)((float)gx + 0.5f, (float)gy + 0.5f) + j;
        float2 d = p - site;
        float dist = length(d);
        float dr = 2.0f * st_rand(cid, 13u, 97u) - 1.0f;
        dist *= 1.0f + distance_random * dr * 0.45f;

        if (dist < f1)
        {
            f2 = f1; f1 = dist;
            float rd = st_rand(cid, 14u, 97u);
            winner_depth = mix(1.0f, 0.25f + 0.75f * rd, depth_random);
        }
        else if (dist < f2) f2 = dist;
    }
    return fmax(f2 - f1, 0.0f) * winner_depth;
}

float st_layered_worley(float2 uv, int cells, int octaves, float persistence, float lacunarity,
                        float jitter, float distance_random, float depth_random, uint seed)
{
    int count = clamp(octaves, 1, 8);
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        if (i >= count) break;
        int ncells = max(1, (int)floor((float)cells * freq + 0.5f));
        float w = st_worley_f2f1(uv, ncells, jitter, distance_random, depth_random, seed + (uint)i * 1013u);
        sum += w * amp; norm += amp;
        amp *= persistence; freq *= lacunarity;
    }
    return norm > 1e-8f ? sum / norm : 0.0f;
}

float st_cliff_profile(float coord, float plateau, float steepness)
{
    float p = clamp(plateau, 0.15f, 0.92f);
    float s = clamp(steepness, 0.0f, 1.0f);
    float half_w = (1.0f - p) * 0.5f;          // renamed from "half"

    float d = fmin(coord, 1.0f - coord);
    if (d > half_w) return 1.0f;

    float t = d / fmax(half_w, 1e-5f);
    float power = mix(1.6f, 8.0f, s);
    return pow(t, power);
}

float st_jagged(float2 uv, float2 offset, int cells, int octaves, float persistence,
                float lacunarity, float jitter, float distance_random, float depth_random, uint seed)
{
    float a = st_layered_worley(st_frac2(uv + offset), cells, octaves, persistence, lacunarity,
                                jitter, distance_random, depth_random, seed ^ 0x31A53F85u);
    float b = st_layered_worley(st_frac2(uv + offset + (float2)(0.371f, 0.613f)), cells, octaves,
                                persistence, lacunarity, jitter, distance_random, depth_random, seed ^ 0xA511E9B3u);
    return a - b;
}

float st_phase(float2 uv, int strata_count, int drift_cycles, float jagged_amount, float2 jagged_offset,
               int cells, int octaves, float persistence, float lacunarity, float jitter,
               float distance_random, float depth_random, uint seed)
{
    float phase = (float)max(strata_count, 1) * uv.y + (float)drift_cycles * uv.x;
    float jag = st_jagged(uv, jagged_offset, cells, octaves, persistence, lacunarity,
                          jitter, distance_random, depth_random, seed);
    phase += jagged_amount * jag;
    return phase;
}

float st_lp(float2 d, float p)
{
    float k = fmax(p, 0.25f);
    return pow(pow(fabs(d.x), k) + pow(fabs(d.y), k), 1.0f / k);
}

void st_worley(float2 uv, int cells, float jitter, float metric, float distance_random,
               float depth_random, uint seed, float *out_f1, float *out_f2, float *out_depth)
{
    int ncells = max(cells, 1);
    float2 p = st_frac2(uv) * (float)ncells;
    int cx = (int)floor(p.x), cy = (int)floor(p.y);
    float f1 = 1e10f, f2 = 1e10f, depth = 1.0f;

    for (int oy = -2; oy <= 2; oy++)
    for (int ox = -2; ox <= 2; ox++)
    {
        int gx = cx + ox, gy = cy + oy;
        int wx = st_wrapi(gx, ncells), wy = st_wrapi(gy, ncells);
        uint cid = st_hash(seed ^ ((uint)(wx + wy * ncells) * 0x9E3779B9u));

        float2 j = (float2)(st_rand(cid, 11u, 97u), st_rand(cid, 12u, 97u));
        j = (j - 0.5f) * jitter;
        float2 site = (float2)((float)gx + 0.5f, (float)gy + 0.5f) + j;
        float dist = st_lp(p - site, metric);
        float rd = 2.0f * st_rand(cid, 13u, 97u) - 1.0f;
        dist *= 1.0f + rd * distance_random * 0.45f;

        if (dist < f1)
        {
            f2 = f1; f1 = dist;
            float rv = st_rand(cid, 14u, 97u);
            depth = mix(1.0f, 0.25f + 0.75f * rv, depth_random);
        }
        else if (dist < f2) f2 = dist;
    }
    *out_f1 = f1; *out_f2 = f2; *out_depth = depth;
}

float st_layered(float2 uv, int cells, int octaves, float persistence, float jitter,
                 float metric, float distance_random, float depth_random, int mode, uint seed)
{
    float amp = 1.0f, norm = 0.0f, sum = 0.0f, freq = 1.0f;
    for (int i = 0; i < 4; i++)
    {
        if (i >= octaves) break;
        int ncells = max(1, (int)floor((float)cells * freq + 0.5f));
        float f1, f2, depth;
        st_worley(uv, ncells, jitter, metric, distance_random, depth_random,
                  seed + (uint)i * 1013u, &f1, &f2, &depth);

        float v;
        if (mode == 0)
            v = clamp(1.0f - f1, 0.0f, 1.0f);
        else
        {
            v = (1.0f - f1) * fmax(f2 - f1, 0.0f) * 2.5f;
            v = clamp(v, 0.0f, 1.0f);
        }
        v *= depth;
        sum += v * amp; norm += amp;
        amp *= persistence; freq *= 2.0f;
    }
    return norm > 1e-8f ? sum / norm : 0.0f;
}

float st_output(float v, float structure, float edge, float output_min, float output_max)
{
    v = clamp(v + structure - edge, 0.0f, 1.0f);
    return mix(output_min, output_max, v);
}

@KERNEL
{
    int2 px = @ixy;
    int2 res = (int2)(@res.x, @res.y);
    float2 imageuv = ((float2)(px.x, px.y) + 0.5f) / (float2)(res.x, res.y);
    float2 uv = @src.bound ? @src : imageuv;
    float uvscale = @uvscale;

    uint base_seed = (uint)@seed_parm;
    if (@seed.bound) base_seed ^= (uint)@seed;
    uint random_seed = base_seed;
    SYSfastRandom(&random_seed);

    float2 vscale = @scale;
    float vrotate = @rotate;
    float2 vtranslate = @translate;

    vrotate += (SYSfastRandom(&random_seed) - 0.5f) * 2.0f * @rand_rotate;
    vtranslate.x += (SYSfastRandom(&random_seed) - 0.5f) * 2.0f * @rand_translate.x;
    vtranslate.y += (SYSfastRandom(&random_seed) - 0.5f) * 2.0f * @rand_translate.y;

    if (@rand_scaleuniform)
    {
        float r = SYSfastRandom(&random_seed);
        vscale *= mix(@rand_scalemin, @rand_scalemax, r);
    }
    else
    {
        vscale.x *= mix(@rand_scalemin.x, @rand_scalemax.x, SYSfastRandom(&random_seed));
        vscale.y *= mix(@rand_scalemin.y, @rand_scalemax.y, SYSfastRandom(&random_seed));
    }

    uvscale *= length(vscale) * M_SQRT1_2_F;
    if (@uvscale.bound) @uvscale.set(uvscale);

    int layers = max(@strata_count, 1);

    // ---- Phase / strata coord ----
    float phase = st_phase(uv, layers, @drift_cycles, @jagged_amount, @jagged_offset,
                           @worley_cells, @worley_octaves, @persistence, @lacunarity,
                           @jitter, @distance_random, @depth_random, base_seed);
    float coord = st_frac(phase);
    int raw_id = (int)floor(phase);
    int layer_id = st_wrapi(raw_id, layers);

    // ---- Cliff profile (hard steps) ----
    float profile = st_cliff_profile(coord, @plateau_width, @cliff_steepness);

    // Finite difference for boundary distance
    float2 du = (float2)(1.0f / (float)max(res.x, 1), 0.0f);
    float2 dv = (float2)(0.0f, 1.0f / (float)max(res.y, 1));

    float px0 = st_phase(uv - du, layers, @drift_cycles, @jagged_amount, @jagged_offset,
                         @worley_cells, @worley_octaves, @persistence, @lacunarity,
                         @jitter, @distance_random, @depth_random, base_seed);
    float px1 = st_phase(uv + du, layers, @drift_cycles, @jagged_amount, @jagged_offset,
                         @worley_cells, @worley_octaves, @persistence, @lacunarity,
                         @jitter, @distance_random, @depth_random, base_seed);
    float py0 = st_phase(uv - dv, layers, @drift_cycles, @jagged_amount, @jagged_offset,
                         @worley_cells, @worley_octaves, @persistence, @lacunarity,
                         @jitter, @distance_random, @depth_random, base_seed);
    float py1 = st_phase(uv + dv, layers, @drift_cycles, @jagged_amount, @jagged_offset,
                         @worley_cells, @worley_octaves, @persistence, @lacunarity,
                         @jitter, @distance_random, @depth_random, base_seed);

    float gx = (px1 - px0) / (2.0f * du.x);
    float gy = (py1 - py0) / (2.0f * dv.y);
    float glen = length((float2)(gx, gy));
    float phase_distance = fmin(coord, 1.0f - coord);
    float boundary = phase_distance / fmax(glen, 1e-6f);

    // ---- Flow (kept weak for horizontal beds) ----
    float wa = st_layered_worley(st_frac2(uv + @flow_offset_a), @worley_cells, @worley_octaves,
                                 @persistence, @lacunarity, @jitter, @distance_random, @depth_random,
                                 base_seed ^ 0x91E10DA5u);
    float wb = st_layered_worley(st_frac2(uv + @flow_offset_b), @worley_cells, @worley_octaves,
                                 @persistence, @lacunarity, @jitter, @distance_random, @depth_random,
                                 base_seed ^ 0xD1B54A35u);
    float2 flowv = (float2)(wa - 0.35f, wb - 0.35f) * @flow_strength;

    flowv *= vscale;
    flowv = _doskew(flowv, @skew);
    flowv = _dorotate(flowv, vrotate);
    flowv += vtranslate;

    float2 mapvec = flowv - @pivot;
    mapvec = _dorotate(mapvec, vrotate);
    mapvec += @pivot;

    int fa = max(@frequency_a, 1);
    int fb = max(@frequency_b, 1);

    // Local copies – required because @uvs / @uvs_b are write-only
    float2 uv_a = st_frac2(uv * (float)fa + mapvec);
    float2 uv_b = st_frac2((uv + @map_b_offset) * (float)fb + mapvec * @map_b_flow_scale);

    // ---- Write intermediate layers ----
    @strata_coord.set(coord);
    @strata_profile.set(profile);
    @boundary_distance.set(boundary);
    @flow.set(flowv);
    @uvs.set(uv_a);
    @uvs_b.set(uv_b);
    @id.set(layer_id);

    // ============================================================
    // Height generation – weaker Worley + clear branch diff + eikonal edge
    // ============================================================
    uint hseed = st_hash((uint)@seed + 0x51ED27u);
    float detail = clamp(@detail, 0.0f, 1.0f);
    float irregularity = clamp(@irregularity, 0.0f, 1.0f);
    float wstr = clamp(@worley_strength, 0.0f, 1.0f);
    float bdiff = clamp(@branch_diff, 0.0f, 1.0f);

    int octaves = clamp(2 + (int)floor(detail * 1.8f + 0.5f), 2, 4);
    float persistence_h = mix(0.22f, 0.36f, detail);          // lower = weaker Worley

    // Stronger differentiation between branches
    float jitter_a = mix(0.22f, 0.48f, irregularity);
    float jitter_b = mix(0.38f, 0.70f, irregularity * bdiff); // B more jittery
    float distance_a = irregularity * 0.55f;
    float distance_b = irregularity * mix(0.25f, 0.55f, bdiff);
    float depth_random = mix(0.40f, 0.80f, irregularity);

    int cells_a = max(@cells, 1);
    int cells_b = max((int)floor((float)@cells * mix(0.55f, 1.15f, bdiff) + 0.5f), 1);

    // Local UVs (already computed earlier)
    float2 uvh_a = st_frac2(uv_a + flowv * @flow_strength);
    float2 uvh_b = st_frac2(uv_b - flowv * (@flow_strength * 0.88f)
                            + (float2)(0.417f, 0.193f) * bdiff);

    // Branch A – smoother, larger features (F1, Euclidean)
    float a = st_layered(uvh_a, cells_a, octaves, persistence_h, jitter_a, 1.0f,
                         distance_a, depth_random, 0, hseed ^ 0xB5297A4Du);

    // Branch B – more cellular / interrupted (F1*(F2-F1), Minkowski < 1)
    float b = st_layered(uvh_b, cells_b, octaves, persistence_h, jitter_b,
                         mix(0.85f, 0.62f, bdiff), distance_b, depth_random, 1,
                         hseed ^ 0x68E31DA4u);

    // Soften both branches so Worley is no longer dominant
    a = smoothstep(0.08f, 0.78f, a);
    b = smoothstep(0.04f, 0.62f, b);

    // Scale by overall Worley strength
    a *= wstr;
    b *= wstr;

    float mn = fmin(a, b);
    float mx = fmax(a, b);

    // ----------------------------------------------------------
    // Structure from strata profile (cliff bias)
    // ----------------------------------------------------------
    float profile_c = clamp(profile, 0.0f, 1.0f);
    float idrand = 2.0f * st_rand(hseed, (uint)layer_id, 701u) - 1.0f;

    float structure = (profile_c - 0.5f) * @strata_mix * @cliff_height_bias * 0.55f;
    structure += idrand * @strata_mix * 0.12f;

    // ----------------------------------------------------------
    // Eikonal / directional edge
    // Uses the already-computed phase gradient (gx, gy)
    // ----------------------------------------------------------
    float2 g = (float2)(gx, gy);
    float glen2 = length(g);
    float2 n = glen2 > 1e-6f ? g / glen2 : (float2)(0.0f, 1.0f);   // strata normal

    // Axis-aware feathering weight
    // feather_axis = 0 → isotropic
    // feather_axis = 1 → stronger falloff along the strata direction (Y+)
    float axis_w = mix(1.0f, fabs(n.y) * 1.35f + fabs(n.x) * 0.45f, @feather_axis);
    axis_w = clamp(axis_w, 0.35f, 1.6f);

    float edge_dist = boundary * axis_w;
    float edge = 1.0f - smoothstep(0.0f, fmax(@edge_sharpness, 1e-6f), edge_dist);
    edge *= @strata_mix * mix(0.28f, 0.48f, @eikonal_strength);

    // ----------------------------------------------------------
    // Final combination
    // ----------------------------------------------------------
    float hmin = st_output(mn, structure, edge, @output_min, @output_max);
    float hmax = st_output(mx, structure, edge, @output_min, @output_max);

    float h;
    if (@final_mode == 1)
        h = hmin;                                   // pure min
    else if (@final_mode == 2)
        h = mix(hmin, hmax, clamp(@final_mix, 0.0f, 1.0f));
    else
        h = hmax;                                   // pure max (usually cliffier)

    @height_min.set(hmin);
    @height_max.set(hmax);
    @height.set(h);
}