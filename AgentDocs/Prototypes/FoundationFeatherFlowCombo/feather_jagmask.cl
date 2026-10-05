#bind layer !&dst float4

#bind parm jag_length int val=3
#bind parm jag_scale_min float val=0.5
#bind parm jag_scale_max float val=2.0
#bind parm seed float val=1234

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

static float hash21(int2 p, float seed)
{
    return hash11((float)p.x * 127.1f + (float)p.y * 311.7f + seed * 74.17f);
}

static float2 voronoi_random2(float2 p, float seed)
{
    int2 base = convert_int2(floor(p));
    float best_dist = 1e20f;
    int2 best_cell = base;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 cell = base + (int2)(x, y);
            float2 feature = convert_float2(cell) + (float2)(
                hash21(cell, seed + 17.13f),
                hash21(cell, seed + 31.71f));
            float2 delta = p - feature;
            float dist = dot(delta, delta);

            if (dist < best_dist)
            {
                best_dist = dist;
                best_cell = cell;
            }
        }
    }

    return (float2)(hash21(best_cell, seed + 47.93f),
                    hash21(best_cell, seed + 63.47f));
}

@KERNEL
{
    int2 p = @ixy;

    float scale_min = fmax(fmin(@jag_scale_min, @jag_scale_max), 0.1f);
    float scale_max = fmax(fmax(@jag_scale_min, @jag_scale_max), scale_min);
    float cell_base = (float)max(@jag_length, 1);
    float small_cell_size = cell_base * scale_min;
    float large_cell_size = cell_base * scale_max;
    float2 pos = convert_float2(p);

    float2 small = voronoi_random2(pos / small_cell_size, @seed);
    float2 large = voronoi_random2(pos / large_cell_size, @seed + 197.31f);

    @dst.set((float4)(small.x, small.y, large.x, large.y));
}
