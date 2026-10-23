#import "sdf.h"

#bind layer src float
#bind layer src_range float
#bind layer flow float2
#bind layer jag_noise float4
#bind layer !&dst float

#bind parm unitdist float val=0.40

#bind parm jag_amount float val=0.35

#bind parm y_bias float val=0.0
#bind parm x_feather float val=1.0
#bind parm flow_strength float val=0.65
#bind parm remap_contrast float val=1.2

#bind parm seed float val=1234


static int wrapi(int x, int n)
{
    int r = x % n;
    return r < 0 ? r + n : r;
}


static float frac1(float x)
{
    return x - floor(x);
}


static float hash11(float x)
{
    x = frac1(x * 0.1031f);
    x *= x + 33.33f;
    x *= x + x;
    return frac1(x);
}



/*
    0  -X
    1  +X
    2  -Y
    3  +Y
    4  -X-Y
    5  -X+Y
    6  +X-Y
    7  +X+Y
*/
static int2 dir_step(int d)
{
    if (d == 0) return (int2)(-1,  0);
    if (d == 1) return (int2)( 1,  0);
    if (d == 2) return (int2)( 0, -1);
    if (d == 3) return (int2)( 0,  1);
    if (d == 4) return (int2)(-1, -1);
    if (d == 5) return (int2)(-1,  1);
    if (d == 6) return (int2)( 1, -1);

    return (int2)(1, 1);
}


static int jag_neighbor(int d, int side)
{
    if (side == 0) return d;

    if (d == 0) return side < 0 ? 4 : 5;
    if (d == 1) return side < 0 ? 6 : 7;
    if (d == 2) return side < 0 ? 4 : 6;
    if (d == 3) return side < 0 ? 5 : 7;
    if (d == 4) return side < 0 ? 0 : 2;
    if (d == 5) return side < 0 ? 0 : 3;
    if (d == 6) return side < 0 ? 2 : 1;

    return side < 0 ? 3 : 1;
}


static int base_enabled(int pass, int d)
{
    if (pass == 0) return d == 0 || d == 2 || d == 7;
    if (pass == 1) return d == 0 || d == 3 || d == 7;
    if (pass == 2) return d == 1 || d == 3 || d == 6;

    return d == 0 || d == 3 || d == 4 || d == 5 || d == 6 || d == 7;
}


static float normalize_pass(float v, float floorv, float ceilv)
{
    floorv = clamp(floorv, 0.0f, 0.49f);
    ceilv  = clamp(ceilv, floorv + 0.001f, 1.0f);

    if (v < 0.0f)
    {
        float n = -v;
        return floorv / (1.0f + n);
    }

    if (v <= 1.0f)
        return mix(floorv, ceilv, v);

    float o = v - 1.0f;
    return ceilv + (1.0f - ceilv) * (o / (1.0f + o));
}


@KERNEL
{
    int2 res = convert_int2(@dst.res);
    int xres = res.x;
    int yres = res.y;

    int2 p = @ixy;

    // src_range is the 2x1 output of featherminmax.cl: min at (0,0), max at (1,0).
    float src_min = @src_range.bufferIndex((int2)(0, 0));
    float src_max = @src_range.bufferIndex((int2)(1, 0));
    float src_span = src_max - src_min;
    float safe_span = fmax(src_span, 1e-6f);

    // Equalize source values to 0..1 before feather processing.
    float src0 = src_span > 1e-6f
        ? clamp((@src.bufferIndex(p) - src_min) / safe_span, 0.0f, 1.0f)
        : 0.0f;
    float acc = src0;

    int iter_count = 4;
    int maxsteps   = max(xres, yres);

    float jag        = clamp(@jag_amount, 0.0f, 1.0f);
    float y_bias     = clamp(@y_bias, -1.0f, 1.0f);
    float flow_strength = clamp(@flow_strength, 0.0f, 1.0f);
    float x_feather  = clamp(@x_feather, 0.0f, 1.0f);
    float remap_contrast = fmax(@remap_contrast, 0.0f);
    float unitdist   = fmax(@unitdist, 1e-6f);


    for (int it = 0; it < iter_count; ++it)
    {
        float fi = (float)it;
        int pass  = it & 3;
        int cycle = it >> 2;

        float falloff = fabs(@dPdx.x) / unitdist;

        int feather_dir = pass == 0;
        if (cycle & 1)
            feather_dir = !feather_dir;

        float up_cand = feather_dir ? -1e20f : 1e20f;
        float down_cand = feather_dir ? -1e20f : 1e20f;
        float horizontal_cand = feather_dir ? -1e20f : 1e20f;
        int active_up = 0;
        int active_down = 0;
        int active_horizontal = 0;


        for (int original_dir = 0; original_dir < 8; ++original_dir)
        {
            if (!base_enabled(pass, original_dir))
                continue;

            int dir = original_dir;
            float dir_cand = feather_dir ? -1e20f : 1e20f;
            float scale_pick = hash11(@seed + fi * 37.17f + (float)dir * 83.29f);
            int2 jag_offset = (int2)(
                (int)(hash11(@seed + fi * 59.17f + (float)dir * 23.29f) * (float)xres),
                (int)(hash11(@seed + fi * 71.31f + (float)dir * 41.73f) * (float)yres));
            int2 q = p;
            float travel = 0.0f;

            for (int s = 0; s < maxsteps; ++s)
            {
                if (travel > 1.0f)
                    break;

                q.x = wrapi(q.x, xres);
                q.y = wrapi(q.y, yres);

                float source_value = @src.bufferIndex(q);
                float sv = src_span > 1e-6f
                    ? clamp((source_value - src_min) / safe_span, 0.0f, 1.0f)
                    : 0.0f;
                float v = feather_dir ? sv - travel : sv + travel;
                dir_cand = feather_dir ? fmax(dir_cand, v) : fmin(dir_cand, v);

                float2 flow_value = @flow.bufferIndex(q);
                float flow_length = length(flow_value);
                float2 flow_dir = flow_length > 1e-6f
                    ? flow_value / flow_length
                    : (float2)(0.0f, 0.0f);
                float flow_influence = clamp(flow_length * flow_strength, 0.0f, 1.0f);

                int2 jag_xy = (int2)(
                    wrapi(q.x + jag_offset.x, xres),
                    wrapi(q.y + jag_offset.y, yres));
                float4 jag_values = @jag_noise.bufferIndex(jag_xy);
                float2 voro = scale_pick < 0.5f ? jag_values.xy : jag_values.zw;

                int walk_dir = dir;
                if (voro.x < jag)
                {
                    int dl = jag_neighbor(dir, -1);
                    int dr = jag_neighbor(dir,  1);
                    float2 sample_dir = -convert_float2(dir_step(dir));
                    float flow_cross = sample_dir.x * flow_dir.y - sample_dir.y * flow_dir.x;
                    float flow_side = flow_cross >= 0.0f ? 0.0f : 1.0f;
                    float side_choice = mix(voro.y, flow_side, flow_influence);
                    walk_dir = side_choice < 0.5f ? dl : dr;
                }

                int2 step = dir_step(walk_dir);
                q -= step;
                float step_length = walk_dir >= 4 ? 1.41421356237f : 1.0f;
                travel += falloff * step_length;
            }

            // Attenuate X-bearing directions, including diagonals, toward the source.
            if (dir == 0 || dir == 1 || dir >= 4)
                dir_cand = mix(src0, dir_cand, x_feather);

            // q -= step reverses the direction labels, so classify by sampled travel.
            int y_direction = (dir == 2 || dir == 4 || dir == 6) ? 1
                : ((dir == 3 || dir == 5 || dir == 7) ? -1 : 0);

            if (y_direction > 0)
            {
                up_cand = feather_dir ? fmax(up_cand, dir_cand) : fmin(up_cand, dir_cand);
                ++active_up;
            }
            else if (y_direction < 0)
            {
                down_cand = feather_dir ? fmax(down_cand, dir_cand) : fmin(down_cand, dir_cand);
                ++active_down;
            }
            else
            {
                horizontal_cand = feather_dir
                    ? fmax(horizontal_cand, dir_cand)
                    : fmin(horizontal_cand, dir_cand);
                ++active_horizontal;
            }
        }

        float pass_cand = feather_dir ? -1e20f : 1e20f;
        int active = active_up + active_down + active_horizontal;

        if (active_up > 0 && active_down > 0)
        {
            float neutral_cand = feather_dir
                ? fmax(up_cand, down_cand)
                : fmin(up_cand, down_cand);
            float bias_target = y_bias >= 0.0f ? up_cand : down_cand;
            pass_cand = mix(neutral_cand, bias_target, fabs(y_bias));
        }
        else if (active_up > 0)
            pass_cand = up_cand;
        else if (active_down > 0)
            pass_cand = down_cand;

        if (active_horizontal > 0)
            pass_cand = active_up + active_down > 0
                ? (feather_dir ? fmax(pass_cand, horizontal_cand) : fmin(pass_cand, horizontal_cand))
                : horizontal_cand;

        if (active == 0)
            pass_cand = src0;

        if ((it % 3) == 2)
            acc = sdfOpDifferenceRound(acc, pass_cand, 0.0f);
        else
            acc = sdfOpIntersectChamfer(acc, pass_cand, 0.0f);

        acc = normalize_pass(acc, 0.0f, 0.5f);
    }

    // Output the processed field; featherminmax + featherrange_remap
    // normalize this complete image into the original source range.
    float normalized = clamp(acc, 0.0f, 1.0f);
    normalized = clamp((normalized - 0.5f) * remap_contrast + 0.5f, 0.0f, 1.0f);
    @dst.set(normalized);
}
