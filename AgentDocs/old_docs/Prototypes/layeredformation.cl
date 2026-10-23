#runover layer
#bind layer &height float
#bind parm seed int val=17
#bind parm enable_formations int val=1
#bind parm enable_kinks int val=1
#bind parm enable_jags int val=1
#bind parm enable_width_variation int val=1
#bind parm enable_variation int val=1
#bind parm enable_tilt int val=1
#bind parm enable_breakup int val=1
#bind parm enable_plates int val=1
#bind parm enable_chips int val=1
#bind parm enable_flakes int val=1
#bind parm enable_cracks int val=1
#bind parm enable_worley int val=1
#bind parm enable_gradient int val=1
#bind parm enable_warp int val=1
#bind parm layer_count int val=9
#bind parm diagonal_turns int val=-3
#bind parm kink_cells int val=4
#bind parm kink_strength float val=0.85
#bind parm jagged_cells int val=28
#bind parm edge_jaggedness float val=0.14
#bind parm ledge_width float val=0.045
#bind parm ledge_width_variation float val=1.25
#bind parm ledge_width_cells int val=6
#bind parm face_tilt float val=0.65
#bind parm height_variation float val=0.30
#bind parm relief float val=1.0
#bind parm worley_octaves int val=3
#bind parm worley_gain float val=0.50
#bind parm worley_jitter float val=0.90
#bind parm worley_softness float val=0.85
#bind parm worley_metric float val=0.65
#bind parm worley_ridge_mix float val=0.85
#bind parm depth_jitter float val=0.35
#bind parm gradient_mix float val=0.30
#bind parm noise_warp float val=0.35
#bind parm kink_worley float val=0.70
#bind parm jag_worley float val=0.85
#bind parm width_worley float val=0.80
#bind parm chip_worley float val=0.75
#bind parm chip_seam_cells int val=7
#bind parm chip_layers int val=2
#bind parm formation_count int val=3
#bind parm automatic_formations int val=1
#bind parm formation_variation float val=0.55
#bind parm formation2_blend float val=0.85
#bind parm formation3_blend float val=-0.55
#bind parm formation2_height float val=0.85
#bind parm formation3_height float val=1.15
#bind parm formation2_offset float val=0.20
#bind parm formation3_offset float val=0.10
#bind parm formation2_phase float val=0.37
#bind parm formation3_phase float val=0.73
#bind parm formation2_frequency int val=2
#bind parm formation3_frequency int val=1
#bind parm formation_fold_coherence float val=0.65
#bind parm thickness_variation float val=0.45
#bind parm tilt_variation float val=0.35
#bind parm face_unevenness float val=0.08
#bind parm breakup_cells int val=5
#bind parm layer_breakup float val=0.55
#bind parm plate_breakup float val=1.0
#bind parm plate_cells int val=6
#bind parm plate_jag float val=0.12
#bind parm plate_gap float val=0.02
#bind parm plate_height float val=0.18
#bind parm plate_tilt float val=0.25
#bind parm layer_length_cells int val=1
#bind parm layer_length_variation float val=0.85
#bind parm chamfer_width float val=0.07
#bind parm chamfer_variation float val=0.65
#bind parm flake_length float val=0.80
#bind parm flake_width float val=0.24
#bind parm flake_tilt float val=0.90
#bind parm flake_angle_variation float val=0.30
#bind parm flake_chamfer float val=0.10
#bind parm corner_chipping float val=0.45
#bind parm flake_count int val=3
#bind parm flake_height float val=0.20
#bind parm flake_coverage float val=0.55
#bind parm chip_density float val=0.18
#bind parm chip_cells int val=45
#bind parm chip_depth float val=0.10
#bind parm crack_count int val=5
#bind parm crack_width float val=0.025
#bind parm crack_depth float val=0.35
#bind parm crack_jaggedness float val=0.35
#bind parm crack_coverage float val=0.65

uint kr_hash(uint h)
{
    h ^= h >> 16; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    return h ^ (h >> 16);
}
int kr_wrap(int x, int period)
{
    return ((x % period) + period) % period;
}
float kr_random(int2 cell, int period, uint seed)
{
    uint x = (uint)kr_wrap(cell.x, period);
    uint y = (uint)kr_wrap(cell.y, period);
    uint key = kr_hash(x * 1597334677u ^ y * 3812015801u ^ seed);
    return (float)(key >> 8) * (1.0f / 16777216.0f);
}
float kr_sample(uint key, uint channel)
{
    return (float)(kr_hash(key ^ kr_hash(channel + 1u)) >> 8)
         * (1.0f / 16777216.0f);
}
float2 kr_grad_dir(int2 cell, int period, uint seed)
{
    float a = 6.2831853f * kr_random(cell, period, seed);
    return (float2)(cos(a), sin(a));
}
float kr_gradient_noise(float2 uv, int cells, uint seed)
{
    float2 p = uv * (float)cells;
    int2 i = convert_int2(floor(p));
    float2 f = p - floor(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float n00 = dot(kr_grad_dir(i, cells, seed), f);
    float n10 = dot(kr_grad_dir(i + (int2)(1, 0), cells, seed), f - (float2)(1.0f, 0.0f));
    float n01 = dot(kr_grad_dir(i + (int2)(0, 1), cells, seed), f - (float2)(0.0f, 1.0f));
    float n11 = dot(kr_grad_dir(i + (int2)(1, 1), cells, seed), f - (float2)(1.0f, 1.0f));
    return 1.41421356f * mix(mix(n00, n10, u.x), mix(n01, n11, u.x), u.y);
}
float kr_gradient_fbm(float2 uv, int cells, int octaves, float gain, uint seed)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    int c = max(cells, 1);
    for (int o = 0; o < max(octaves, 1) && c <= (1 << 24); ++o)
    {
        sum += amp * kr_gradient_noise(uv, c, kr_hash(seed + (uint)o * 2654435761u));
        norm += amp;
        amp *= gain;
        c *= 2;
    }
    float safe_norm = copysign(fmax(fabs(norm), 0.0001f), norm);
    return sum / safe_norm;
}

float3 kr_worley_point(int2 cell, int cells, uint seed, float jitter)
{
    uint key = kr_hash((uint)kr_wrap(cell.x, cells) * 1597334677u
             ^ (uint)kr_wrap(cell.y, cells) * 3812015801u ^ seed);
    float2 offset = (float2)(kr_sample(key, 1u), kr_sample(key, 2u)) - 0.5f;
    float2 point = convert_float2(cell) + 0.5f + jitter * offset;
    return (float3)(point.x, point.y, kr_sample(key, 0u));
}

float kr_dist(float2 a, float2 b, float metric)
{
    float2 d = fabs(a - b);
    float eucl = length(d);
    float cheb = fmax(d.x, d.y);
    return mix(eucl, cheb, clamp(metric, 0.0f, 1.0f));
}

float4 kr_worley_layer(float2 uv, int cells, uint seed, float jitter, float softness, float metric)
{
    float2 p = uv * (float)cells;
    int2 base = convert_int2(floor(p));
    int search = max((int)ceil(0.5f + 0.5f * fabs(jitter)), 1);

    float f1 = 1.0e9f;
    float f2 = 1.0e9f;
    float nearest_id = 0.0f;

    for (int y = -search; y <= search; ++y)
    for (int x = -search; x <= search; ++x)
    {
        float3 q = kr_worley_point(base + (int2)(x, y), cells, seed, jitter);
        float d = kr_dist(p, q.xy, metric);
        if (d < f1)
        {
            f2 = f1;
            f1 = d;
            nearest_id = q.z;
        }
        else if (d < f2)
        {
            f2 = d;
        }
    }

    float k = fmin(60.0f * pow(1.0f / 12.0f, softness), 1.0e6f);
    float kd = fmax(k, 5.0f);
    float esum = 0.0f;
    float wsum = 0.0f;
    float idsum = 0.0f;

    for (int y = -search; y <= search; ++y)
    for (int x = -search; x <= search; ++x)
    {
        float3 q = kr_worley_point(base + (int2)(x, y), cells, seed, jitter);
        float d = kr_dist(p, q.xy, metric);
        float window = 1.0f - smoothstep(0.7f, 1.0f, d);
        float w = exp(-k * (d * d - f1 * f1)) * window;
        wsum += w;
        idsum += w * q.z;
        esum += exp(-kd * (d - f1));
    }

    float soft_id = (idsum + 0.001f * nearest_id) / (wsum + 0.001f);
    float soft_f1 = f1 - log(esum + 1e-6f) / kd;

    return (float4)(soft_id, soft_f1, f1, f2);
}

float3 kr_worley_fbm(float2 uv, int cells, int octaves, float gain, float jitter,
                     float softness, float warp, float metric, float ridge_mix, uint seed)
{
    float3 sum = (float3)(0.0f);
    float amp = 1.0f;
    float norm = 0.0f;
    int c = max(cells, 1);
    float ridge_width = fmax(mix(0.22f, 0.85f, softness), 0.001f);

    for (int o = 0; o < max(octaves, 1) && c <= (1 << 24); ++o)
    {
        float2 suv = uv;
        if (warp != 0.0f)
        {
            int wc = max(c / 2, 1);
            suv += warp / (float)c
                 * (float2)(kr_gradient_noise(uv, wc, seed ^ 0x51ed270bu),
                            kr_gradient_noise(uv, wc, seed ^ 0x6a09e667u));
        }

        float4 w = kr_worley_layer(suv, c, kr_hash(seed + (uint)o * 747796405u),
                                   jitter, softness, metric);

        float f2f1 = w.w - w.z;
        float ridge = 1.0f - smoothstep(0.0f, ridge_width, f2f1);
        float cone  = 1.0f - w.y * 1.25f;
        float shaped = mix(cone, ridge, ridge_mix);

        sum += amp * (float3)(w.x, shaped, ridge);
        norm += amp;
        amp *= gain;
        c *= 2;
    }

    float safe_norm = copysign(fmax(fabs(norm), 0.0001f), norm);
    return sum / safe_norm;
}

float kr_angular_noise(float2 uv, int cells, uint seed)
{
    float2 p = uv * (float)cells;
    int2 cell = convert_int2(floor(p));
    float2 f = p - floor(p);
    float a = kr_random(cell, cells, seed);
    float b = kr_random(cell + (int2)(1, 0), cells, seed);
    float c = kr_random(cell + (int2)(0, 1), cells, seed);
    float d = kr_random(cell + (int2)(1, 1), cells, seed);
    float value;
    if (f.x + f.y <= 1.0f)
        value = a + f.x * (b - a) + f.y * (c - a);
    else
        value = d + (1.0f - f.y) * (b - d) + (1.0f - f.x) * (c - d);
    return 2.0f * value - 1.0f;
}

float4 kr_plates(float2 uv, int cells, uint seed, float jitter, float warp, float jag)
{
    int c = max(cells, 1);
    float2 suv = uv;
    if (warp != 0.0f)
    {
        int wc = max(c / 2, 1);
        suv += warp / (float)c
             * (float2)(kr_gradient_noise(uv, wc, seed ^ 0x51ed270bu),
                        kr_gradient_noise(uv, wc, seed ^ 0x6a09e667u));
    }
    if (jag != 0.0f)
        suv += jag / (float)c
             * (float2)(kr_angular_noise(uv, c * 4, seed ^ 0x1f83d9abu),
                        kr_angular_noise(uv, c * 4, seed ^ 0x5be0cd19u));
    float2 p = suv * (float)c;
    int2 base = convert_int2(floor(p));
    int search = max((int)ceil(0.5f + 0.5f * fabs(jitter)), 1);
    float d1 = 1.0e9f;
    float d2 = 1.0e9f;
    float id = 0.0f;
    float2 center = (float2)(0.0f, 0.0f);
    for (int y = -search; y <= search; ++y)
    for (int x = -search; x <= search; ++x)
    {
        float3 q = kr_worley_point(base + (int2)(x, y), c, seed, jitter);
        float d = length(p - q.xy);
        if (d < d1)
        {
            d2 = d1;
            d1 = d;
            id = q.z;
            center = q.xy;
        }
        else if (d < d2)
        {
            d2 = d;
        }
    }
    return (float4)(id, 0.5f * (d2 - d1), p.x - center.x, p.y - center.y);
}

float kr_shaped(float2 uv, int cells, uint seed, int octaves, float gain, float jitter,
                float softness, float warp, float gmix,
                float wmix, float w_id, float w_cone, float w_ridge,
                float metric, float ridge_mix)
{
    float value = kr_angular_noise(uv, max(cells, 1), seed);
    if (wmix != 0.0f)
    {
        float3 w = kr_worley_fbm(uv, cells, octaves, gain, jitter, softness, warp,
                                 metric, ridge_mix, seed ^ 0xa511e9b3u);
        float shaped = 2.0f * (w_id * w.x + w_cone * w.y + w_ridge * w.z) - 1.0f;
        value = mix(value, shaped, wmix);
    }
    if (gmix != 0.0f)
        value = mix(value, kr_gradient_fbm(uv, max(cells, 1), octaves, gain,
                                           seed ^ 0x3c6ef372u), gmix);
    return value;
}

float kr_chip_events(float2 uv, int cells, uint seed, float density)
{
    if (density <= 0.0f) return 0.0f;
    float2 p = uv * (float)cells;
    int2 base = convert_int2(floor(p));
    float result = 0.0f;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        int2 cell = base + (int2)(x, y);
        uint key = kr_hash((uint)kr_wrap(cell.x, cells) * 1597334677u
                 ^ (uint)kr_wrap(cell.y, cells) * 3812015801u ^ seed);
        if (kr_sample(key, 0u) >= density) continue;
        float2 center = convert_float2(cell)
                      + (float2)(kr_sample(key, 1u), kr_sample(key, 2u));
        float2 radius = (float2)(mix(0.15f, 0.45f, kr_sample(key, 3u)),
                                mix(0.15f, 0.45f, kr_sample(key, 4u)));
        float2 d = fabs((p - center) / radius);
        float triangle = fmax(1.0f - d.x - d.y, 0.0f);
        float floor_at = kr_sample(key, 5u) < 0.5f ? 0.67f : 1.0f;
        float bite = fmin(triangle, floor_at) / floor_at;
        result = fmax(result, bite * mix(0.5f, 1.0f, kr_sample(key, 6u)));
    }
    return result;
}

float kr_chip_layers(float2 uv, int cells, uint seed, float density, int layers)
{
    float result = 0.0f;
    float weight = 1.0f;
    int c = max(cells, 1);
    for (int l = 0; l < max(layers, 1) && c <= (1 << 24); ++l)
    {
        uint layer_seed = l == 0 ? seed : kr_hash(seed + (uint)l * 2891336453u);
        result = fmax(result, weight * kr_chip_events(uv, c, layer_seed, density));
        weight *= 0.55f;
        c *= 2;
    }
    return result;
}

float kr_flakes(float2 uv, float2 axis, int cells, int count, uint seed,
    float coverage, float length_control, float width_control, float angle_variation,
    float tilt, float chamfer, float corner_chipping, float chip_depth, float chip_density)
{
    if (length_control <= 0.0f || width_control <= 0.0f) return 0.0f;
    float2 p = uv * (float)cells;
    int2 base = convert_int2(floor(p));
    float result = 0.0f;
    int search = (int)ceil(0.5f * length((float2)(length_control, width_control)));
    for (int y = -search; y <= search; ++y)
    for (int x = -search; x <= search; ++x)
    {
        int2 cell = base + (int2)(x, y);
        uint cell_key = kr_hash((uint)kr_wrap(cell.x, cells) * 1597334677u
                     ^ (uint)kr_wrap(cell.y, cells) * 3812015801u ^ seed);
        for (int i = 0; i < count; ++i)
        {
            uint key = kr_hash(cell_key ^ ((uint)i + 1u) * 747796405u);
            if (kr_sample(key, 0u) >= coverage) continue;
            float2 center = convert_float2(cell)
                          + (float2)(kr_sample(key, 1u), kr_sample(key, 2u));
            float angle = (2.0f * kr_sample(key, 3u) - 1.0f) * angle_variation;
            float2 along = (float2)(axis.x * cos(angle) - axis.y * sin(angle),
                                   axis.x * sin(angle) + axis.y * cos(angle));
            float2 across = (float2)(-along.y, along.x);
            float2 delta = p - center;
            float2 q = (float2)(dot(delta, along), dot(delta, across));
            float hx = 0.5f * length_control
                     * mix(0.35f, 1.0f, kr_sample(key, 4u));
            float hy = 0.5f * width_control
                     * mix(0.35f, 1.0f, kr_sample(key, 5u));
            float inset = fmin(hx - fabs(q.x), hy - fabs(q.y));
            if (inset <= 0.0f) continue;
            for (int corner = 0; corner < 4; ++corner)
            {
                if (kr_sample(key, 30u + (uint)corner) >= chip_density) continue;
                float sx = (corner & 1) ? 1.0f : -1.0f;
                float sy = (corner & 2) ? 1.0f : -1.0f;
                float cut = corner_chipping
                          * mix(0.2f, 1.0f, kr_sample(key, 10u + (uint)corner));
                float plane = (2.0f - cut - sx * q.x / hx - sy * q.y / hy)
                            / sqrt(1.0f / (hx * hx) + 1.0f / (hy * hy));
                inset = fmin(inset, plane);
            }
            float notch_center = mix(-hx, hx, kr_sample(key, 20u));
            float notch_depth = kr_sample(key, 34u) < chip_density
                              ? chip_depth * hy * kr_sample(key, 21u) : 0.0f;
            float notch_side = kr_sample(key, 22u) < 0.5f ? -1.0f : 1.0f;
            float notch = (hy - notch_side * q.y - notch_depth
                         + fabs(q.x - notch_center)) * 0.70710678f;
            inset = fmin(inset, notch);
            float bevel = clamp(inset / fmax(chamfer * fmin(hx, hy), 0.0001f),
                                0.0f, 1.0f);
            float ramp_t = clamp(0.5f + 0.5f * q.y / hy, 0.0f, 1.0f);
            float ramp = 1.0f - tilt * ramp_t;
            result = fmax(result, bevel * ramp * mix(0.45f, 1.0f, kr_sample(key, 6u)));
        }
    }
    return result;
}

float kr_bevel(float inset, float width)
{
    return clamp(inset / fmax(width, 0.0001f), 0.0f, 1.0f);
}
float kr_fraction(float value)
{
    return value - floor(value);
}

@KERNEL
{
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@height.res);
    int layers = max(@layer_count, 1);
    uint root_seed = (uint)@seed;
    int wo = @worley_octaves;
    float wg = @worley_gain;
    float wj = @worley_jitter;
    float ws = @worley_softness;
    float metric = @worley_metric;
    float ridge_mix = @worley_ridge_mix;
    float depth_j = @depth_jitter;

    bool on_formations = @enable_formations != 0;
    bool on_kinks = @enable_kinks != 0;
    bool on_jags = @enable_jags != 0;
    bool on_widthvar = @enable_width_variation != 0;
    bool on_var = @enable_variation != 0;
    bool on_tilt = @enable_tilt != 0;
    bool on_breakup = @enable_breakup != 0;
    bool on_plates = @enable_plates != 0;
    bool on_chips = @enable_chips != 0;
    bool on_flakes = @enable_flakes != 0;
    bool on_cracks = @enable_cracks != 0;
    bool on_worley = @enable_worley != 0;
    float wp = @enable_warp != 0 ? @noise_warp : 0.0f;
    float gm = @enable_gradient != 0 ? @gradient_mix : 0.0f;
    float kink_wm = on_worley ? @kink_worley : 0.0f;
    float jag_wm = on_worley ? @jag_worley : 0.0f;
    float width_wm = on_worley ? @width_worley : 0.0f;
    float chip_wm = on_worley ? @chip_worley : 0.0f;
    float corner_chip = on_chips ? @corner_chipping : 0.0f;

    float shared_kink = on_kinks
        ? kr_shaped(uv, max(@kink_cells, 1), root_seed, wo, wg, wj, ws, wp, gm,
                    kink_wm, 0.20f, 0.80f, 0.0f, metric, ridge_mix)
        : 0.0f;

    float stack = 0.0f;
    int formations = on_formations ? clamp(@formation_count, 1, 3) : 1;

    for (int formation = 0; formation < formations; ++formation)
    {
        uint seed = formation == 0 ? root_seed
                  : kr_hash(root_seed ^ (uint)formation * 747796405u);
        int frequency = formation == 0 ? 1 : max(formation == 1
                      ? @formation2_frequency : @formation3_frequency, 1);
        float phase_offset = formation == 0 ? 0.0f : (formation == 1
                           ? @formation2_phase : @formation3_phase);
        float formation_height = formation == 0 ? 1.0f : (formation == 1
                               ? @formation2_height : @formation3_height);
        float elevation = formation == 0 ? 0.0f : (formation == 1
                        ? @formation2_offset : @formation3_offset);

        if (@automatic_formations && formation > 0)
        {
            float diversity = @formation_variation;
            phase_offset = kr_sample(seed, 100u);
            frequency = kr_sample(seed, 101u) < diversity * 0.5f ? 2 : 1;
            formation_height = 1.0f + diversity * (kr_sample(seed, 102u) - 0.5f);
            elevation = diversity * 0.35f * kr_sample(seed, 103u);
        }

        float blend = formation == 0 ? 1.0f : (formation == 1
                    ? @formation2_blend : @formation3_blend);
        if (formation > 0 && blend == 0.0f) continue;

        float kink = shared_kink;
        if (on_kinks && formation > 0 && @formation_fold_coherence != 1.0f)
            kink = mix(kr_shaped(uv, max(@kink_cells, 1), seed, wo, wg, wj, ws, wp, gm,
                                kink_wm, 0.20f, 0.80f, 0.0f, metric, ridge_mix),
                       shared_kink, @formation_fold_coherence);

        float jag = 0.0f;
        float jag_face = 0.0f;
        if (on_jags)
        {
            float jag_angular = kr_angular_noise(uv, max(@jagged_cells, 1), seed ^ 2891336453u);
            jag = jag_angular;
            jag_face = jag_angular;
            if (jag_wm != 0.0f)
            {
                float3 jw = kr_worley_fbm(uv, max(@jagged_cells, 1), wo, wg, wj, ws, wp,
                                          metric, ridge_mix,
                                          (seed ^ 2891336453u) ^ 0xa511e9b3u);
                jag = mix(jag_angular, 2.0f * (0.35f * jw.y + 0.65f * jw.z) - 1.0f, jag_wm);
                jag_face = mix(jag_angular, 2.0f * jw.y - 1.0f, jag_wm);
            }
            if (gm != 0.0f)
            {
                float jg = kr_gradient_fbm(uv, max(@jagged_cells, 1), wo, wg,
                                           (seed ^ 2891336453u) ^ 0x3c6ef372u);
                jag = mix(jag, jg, gm);
                jag_face = mix(jag_face, jg, gm);
            }
        }

        int patches = max(@breakup_cells, 1);
        float thickness = on_var ? kr_angular_noise(uv, patches, seed ^ 747796405u) : 0.0f;
        float phase = (uv.y * (float)layers + uv.x * (float)@diagonal_turns)
                    * (float)frequency + phase_offset
                    + @kink_strength * kink
                    + @edge_jaggedness * jag
                    + @thickness_variation * thickness;
        float t = kr_fraction(phase);
        float variation = on_var ? 0.5f + 0.5f * kr_angular_noise(
            uv, max(@kink_cells, 1), seed ^ 1013904223u) : 1.0f;
        float amplitude = mix(1.0f, variation, @height_variation);
        float tilt_noise = on_var ? kr_angular_noise(uv, patches, seed ^ 277803737u) : 0.0f;
        float tilt = on_tilt ? @face_tilt + @tilt_variation * tilt_noise : 0.0f;
        float width_field = on_widthvar
            ? kr_shaped(uv, max(@ledge_width_cells, 1), seed ^ 0x68bc21ebu,
                        wo, wg, wj, ws, wp, gm, width_wm, 0.70f, 0.30f, 0.0f,
                        metric, ridge_mix)
            : 0.0f;
        float width_scale = exp2(@ledge_width_variation * width_field);
        float width = @ledge_width * width_scale;
        float face_span = 1.0f - 2.0f * width;
        face_span = copysign(fmax(fabs(face_span), 0.0001f), face_span);
        float t_face = t + @face_unevenness * jag_face;
        float face = clamp((t_face - width) / face_span, 0.0f, 1.0f);
        float ramp = 1.0f - tilt * face;

        float chip_event = 0.0f;
        if (on_chips)
        {
            chip_event = kr_chip_layers(uv, max(@chip_cells, 1),
                                        seed ^ 2246822519u, @chip_density, @chip_layers);
            if (chip_wm != 0.0f && chip_event != 0.0f)
            {
                float3 seam_w = kr_worley_fbm(uv, max(@chip_seam_cells, 1), wo, wg, wj, ws, wp,
                                              metric, ridge_mix, seed ^ 0x2c1b3c6du);
                float seam = 0.7f * seam_w.z + 0.3f * (1.0f - seam_w.y);
                chip_event = chip_event * mix(1.0f, 0.3f + 1.4f * seam, chip_wm);
            }
        }
        float chip = @chip_depth * chip_event;
        float chamfer = @chamfer_width * mix(1.0f, variation, @chamfer_variation);
        float bevel_width = fmax(width, chamfer * width_scale);
        float breakup = @layer_breakup;
        float termination = 1.0f;

        if (on_breakup && breakup != 0.0f && !(on_plates && @plate_breakup == 1.0f))
        {
            float2 length_uv = (float2)(uv.x * (float)max(@layer_length_cells, 1), uv.y);
            float size_noise = 0.0f;
            if (@layer_length_variation != 0.0f)
            {
                float length_noise = kr_angular_noise(uv, patches, seed ^ 0x9e3779b9u);
                size_noise = kr_angular_noise(uv, patches, seed ^ 0x85ebca6bu);
                float2 layer_axis = normalize((float2)((float)layers, -(float)@diagonal_turns));
                length_uv += layer_axis * (@layer_length_variation * length_noise / (float)patches);
            }
            float patch = kr_angular_noise(length_uv, patches, seed ^ 3266489917u);
            termination = mix(1.0f, patch + @layer_length_variation * size_noise, breakup);
        }

        float plate_off = 0.0f;
        if (on_plates && @plate_breakup != 0.0f)
        {
            float4 pl = kr_plates(uv, max(@plate_cells, 1), seed ^ 0x2545f491u, wj, wp,
                                  @plate_jag);
            uint pkey = (uint)(pl.x * 16777216.0f);
            float pa = 6.2831853f * kr_sample(pkey, 41u);
            float plate_term = fmin(pl.y - @plate_gap, (pl.x - 0.5f * breakup) * 4.0f);
            termination = mix(termination, plate_term, @plate_breakup);
            plate_off = @plate_breakup * (@plate_height * (2.0f * kr_sample(pkey, 40u) - 1.0f)
                      + @plate_tilt * dot((float2)(cos(pa), sin(pa)), pl.zw));
        }

        float band_inset = fmin(t, 1.0f - t);
        float corner_cut = corner_chip * bevel_width * chip_event;
        float corner_inset = (band_inset + termination - corner_cut) * 0.70710678f;
        float inset = fmin(fmin(band_inset, termination), corner_inset);
        float edge = kr_bevel(inset, bevel_width + chip);
        float sheet = amplitude * (ramp + plate_off);
        float bed = amplitude * (1.0f - tilt);
        float surface = mix(bed, sheet, edge);

// Depth jitter from cell distance + ID
        if (depth_j > 0.0f && on_worley)
        {
            float3 wj_depth = kr_worley_fbm(uv, max(@kink_cells, 1), 2, 0.55f, wj, ws, wp * 0.5f,
                                            metric, ridge_mix, seed ^ 0x9e3779b9u);
            float cell_depth = (2.0f * wj_depth.x - 1.0f) * (0.4f + 0.6f * (1.0f - wj_depth.y));
            surface += depth_j * amplitude * cell_depth * edge;
        }
        
        float2 flake_uv = uv + (float2)(0.0f, @kink_strength * kink / (float)layers);
        float2 flake_axis = normalize((float2)((float)layers, -(float)@diagonal_turns));
        float flakes = 0.0f;
        if (on_flakes && edge != 0.0f && @flake_height != 0.0f && amplitude != 0.0f)
            flakes = kr_flakes(flake_uv, flake_axis, patches, @flake_count,
                seed ^ 3812015801u, @flake_coverage,
                @flake_length, @flake_width, @flake_angle_variation,
                @flake_tilt, @flake_chamfer, corner_chip,
                on_chips ? @chip_depth : 0.0f, on_chips ? @chip_density : 0.0f);
        surface += edge * @flake_height * amplitude * flakes;

        float crack = 0.0f;
        if (on_cracks && @crack_count > 0 && @crack_width > 0.0f && @crack_depth != 0.0f)
        {
            float crack_phase = uv.x * (float)max(@crack_count, 1)
                              + @crack_jaggedness * (kink + jag);
            float ct = kr_fraction(crack_phase);
            float crack_distance = fmin(ct, 1.0f - ct);
            float crack_patch = kr_angular_noise(uv, patches, seed ^ 1597334677u);
            float crack_end = crack_patch + 2.0f * @crack_coverage - 1.0f;
            float crack_inset = fmin(@crack_width - crack_distance, crack_end);
            crack = kr_bevel(crack_inset, fmax(chamfer, @crack_width * 0.25f));
        }
        surface = surface - @crack_depth * crack;
        surface = formation_height * surface + elevation;

        if (formation == 0)
            stack = surface;
        else
        {
            float combined = blend >= 0.0f ? fmax(stack, surface) : fmin(stack, surface);
            stack = mix(stack, combined, fabs(blend));
        }
    }
    @height.set(@relief * stack);
}