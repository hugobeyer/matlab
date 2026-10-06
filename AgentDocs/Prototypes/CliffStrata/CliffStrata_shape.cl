#import "sdf.h"

#bind layer src float
#bind layer flow float2
// Connect CliffStrata_blocks outputs: block_id -> id, row_id -> row_id, flow -> flow.
#bind layer id int
#bind layer row_id int
#bind layer !&dst float
#bind layer !&voronoi_raw float
#bind layer !&id_distance float
#bind layer !&row_distance float
#bind parm unitdist float val=0.3
#bind parm unitdist_id_lerp float val=0.5
#bind parm carve_depth float val=0.375
#bind parm carve_voronoi float val=0.1
#bind parm y_bias float val=0.3
#bind parm y_bias_voronoi float val=0.9
#bind parm y_bias_voronoi_invert int val=0
#bind parm neg_y_unitdist_taper float val=1.0
#bind parm dir int val=0
#bind parm seed float val=1234
#bind parm voronoi_cells int val=13
#bind parm flow_voronoi float val=0.0
#bind parm chamfer_width float val=0.5
#bind parm chamfer_intensity float val=1.0
#bind parm chamfer_voronoi float val=0.05
#bind parm block_cavity_width float val=0.02
#bind parm row_cavity_width float val=0.25
#bind parm cavity_intensity float val=0.25
#bind parm cavity_voronoi_threshold float val=0.5
#bind parm cavity_voronoi_mask_gain float val=0.125

// Branchless wrap
static int wrapi(int x, int n)
{
    int r = x % n;
    return r + ((r >> 31) & n);
}

static float frac1(float x)
{
    return x - floor(x);
}

static float hash11(float x)
{
    x = frac1(x * 0.1031f);
    x *= x + 33.33f;
    return frac1(x * (x + x));
}

static float hash31i(int3 p, float seed)
{
    return hash11((float)p.x * 127.1f +
                  (float)p.y * 311.7f +
                  (float)p.z * 191.999f +
                  seed * 74.17f);
}

static float3 hash33i(int3 p, float seed)
{
    return (float3)(
        hash31i(p, seed + 17.13f),
        hash31i(p, seed + 71.91f),
        hash31i(p, seed + 143.73f));
}

static float3 hash_id3(int id, float seed)
{
    float f = (float)id;
    return (float3)(
        hash11(f * 127.1f  + seed * 17.13f),
        hash11(f * 311.7f  + seed * 71.91f),
        hash11(f * 191.999f + seed * 143.73f));
}

static float2 warp_voronoi_uv(float2 uv, float height, float2 flow, float amount)
{
    float2 axis = flow / fmax(length(flow), 1e-6f);
    float2 across = (float2)(-axis.y, axis.x);
    float bend = sin(height * 6.28318530718f);
    return uv + amount * (axis * height + across * bend * 0.35f);
}

static float id_unitdist_scale(int id, float seed, float lerp)
{
    float rnd = hash11((float)id * 12.9898f + seed * 78.233f);
    return mix(1.0f, 0.5f + rnd, lerp);
}

// Manually unrolled 3×3×3 Voronoi
static void voronoi3(
    float3 P,
    int pxy,
    int pz,
    float jitter,
    float seed,
    float *f1,
    float *f2)
{
    int3 b = convert_int3(floor(P));
    float d1 = 1e20f;
    float d2 = 1e20f;

    #define VORO_CELL(dx,dy,dz)                                   \
    {                                                             \
        int3 c  = b + (int3)(dx,dy,dz);                           \
        int3 cw = (int3)(wrapi(c.x,pxy), wrapi(c.y,pxy), wrapi(c.z,pz)); \
        float3 r  = hash33i(cw, seed);                            \
        float3 fp = convert_float3(c) + 0.5f + (r - 0.5f) * jitter; \
        float  d  = dot(fp - P, fp - P);                          \
        if (d < d1) { d2 = d1; d1 = d; }                          \
        else if (d < d2) d2 = d;                                  \
    }

    VORO_CELL(-1,-1,-1) VORO_CELL( 0,-1,-1) VORO_CELL( 1,-1,-1)
    VORO_CELL(-1, 0,-1) VORO_CELL( 0, 0,-1) VORO_CELL( 1, 0,-1)
    VORO_CELL(-1, 1,-1) VORO_CELL( 0, 1,-1) VORO_CELL( 1, 1,-1)

    VORO_CELL(-1,-1, 0) VORO_CELL( 0,-1, 0) VORO_CELL( 1,-1, 0)
    VORO_CELL(-1, 0, 0) VORO_CELL( 0, 0, 0) VORO_CELL( 1, 0, 0)
    VORO_CELL(-1, 1, 0) VORO_CELL( 0, 1, 0) VORO_CELL( 1, 1, 0)

    VORO_CELL(-1,-1, 1) VORO_CELL( 0,-1, 1) VORO_CELL( 1,-1, 1)
    VORO_CELL(-1, 0, 1) VORO_CELL( 0, 0, 1) VORO_CELL( 1, 0, 1)
    VORO_CELL(-1, 1, 1) VORO_CELL( 0, 1, 1) VORO_CELL( 1, 1, 1)

    #undef VORO_CELL

    *f1 = sqrt(d1);
    *f2 = sqrt(d2);
}

@KERNEL
{
    int2 res = convert_int2(@dst.res);
    int xr = res.x;
    int yr = res.y;

    // ========================================================
    // PASS 0 – Voronoi modulation (hardcoded cells + jitter)
    // ========================================================
    if (@Iteration == 0)
    {
        int2 p = @ixy;
        float h = @src.bufferIndex(p);

        // Hardcoded
        int cxy = max(@voronoi_cells, 1);
        const int cz  = 6;
        const float jitter = 0.85f;
        const float id_offset = 1.0f;

        float2 uv = (convert_float2(p) + 0.5f) / convert_float2(res);
        uv = warp_voronoi_uv(uv, h, @flow.bufferIndex(p), @flow_voronoi);

        int iid = @id.bufferIndex(p);
        float3 shift = (hash_id3(iid, @seed + 913.73f) - 0.5f) * id_offset;

        float3 P = (float3)(uv.x * (float)cxy,
                            uv.y * (float)cxy,
                            h     * (float)cz) + shift;

        float f1, f2;
        voronoi3(P, cxy, cz, jitter, @seed, &f1, &f2);

        float v = clamp((f2 - f1) * 2.0f, 0.0f, 1.0f);
        @dst.set(v);
        @voronoi_raw.set(v);
        return;
    }

    // ========================================================
    // PASS 1 – 3×3 AA from immutable raw Voronoi (no in-place neighbour reads)
    // ========================================================
    if (@Iteration == 1)
    {
        int2 p = @ixy;
        int xm = wrapi(p.x - 1, @xres);
        int xp = wrapi(p.x + 1, @xres);
        int ym = wrapi(p.y - 1, @yres);
        int yp = wrapi(p.y + 1, @yres);

        float c = @voronoi_raw.bufferIndex(p);
        float b = c * 0.25f +
                  (@voronoi_raw.bufferIndex((int2)(xm, p.y)) +
                   @voronoi_raw.bufferIndex((int2)(xp, p.y)) +
                   @voronoi_raw.bufferIndex((int2)(p.x, ym)) +
                   @voronoi_raw.bufferIndex((int2)(p.x, yp))) * 0.125f +
                  (@voronoi_raw.bufferIndex((int2)(xm, ym)) +
                   @voronoi_raw.bufferIndex((int2)(xp, ym)) +
                   @voronoi_raw.bufferIndex((int2)(xm, yp)) +
                   @voronoi_raw.bufferIndex((int2)(xp, yp))) * 0.0625f;

        @dst.set(b);
        @row_distance.set(b);
        return;
    }

    // ========================================================
    // PASS 2 – Horizontal eikonal
    // ========================================================
    if (@Iteration == 2)
    {
        int y = @iy;
        if (y >= yr) return;

        float dx = fmax(fabs(@dPdx.x), 1e-8f);
        float base_u = fmax(fabs(@unitdist), 1e-6f);

        // LEFT → RIGHT
        int2 p = (int2)(0, y);
        float val = @src.bufferIndex(p);

        for (int x = 0; x < xr; ++x)
        {
            p = (int2)(x, y);
            float h = @src.bufferIndex(p);
            float v = clamp(@dst.bufferIndex(p), 0.0f, 1.0f);
            float signedV = (v * 2.0f - 1.0f) *
                            fmax(0.0f, 1.0f + (v * 2.0f - 1.0f) * @carve_voronoi);

            int iid = @id.bufferIndex(p);
            float scale = id_unitdist_scale(iid, @seed, @unitdist_id_lerp);
            float u = base_u * scale;

            float falloff = (dx / u) * fmax(0.05f, 1.0f + signedV * @carve_depth);

            if (x == 0)
                val = h;
            else if (@dir)
            {
                val -= falloff;
                val = fmax(val, h);
            }
            else
            {
                val += falloff;
                val = fmin(val, h);
            }

            @dst.setIndex(p, val);
        }

        // RIGHT → LEFT
        p = (int2)(xr - 1, y);
        val = @src.bufferIndex(p);

        for (int x = xr - 1; x >= 0; --x)
        {
            p = (int2)(x, y);
            float h = @src.bufferIndex(p);
            float current = @dst.bufferIndex(p);
            float depth = fabs(current - h);

            int iid = @id.bufferIndex(p);
            float scale = id_unitdist_scale(iid, @seed, @unitdist_id_lerp);
            float u = base_u * scale;

            float modulation = clamp(depth / u, 0.0f, 1.0f);
            float voronoi = clamp(@row_distance.bufferIndex(p), 0.0f, 1.0f);
            float carve_scale = fmax(0.0f, 1.0f + (voronoi * 2.0f - 1.0f) * @carve_voronoi);
            float signedV = (modulation * 2.0f - 1.0f) * carve_scale;
            float falloff = (dx / u) * fmax(0.05f, 1.0f + signedV * @carve_depth);

            if (x == xr - 1)
                val = h;
            else if (@dir)
            {
                val -= falloff;
                val = fmax(val, h);
                val = fmax(val, current);
            }
            else
            {
                val += falloff;
                val = fmin(val, h);
                val = fmin(val, current);
            }

            @dst.setIndex(p, val);
        }
        return;
    }

    // ========================================================
    // PASS 3 – Vertical eikonal (with Y bias)
    // ========================================================
    if (@Iteration == 3)
    {
        int lane = @iy;
        float dy = fmax(fabs(@dPdy.y), 1e-8f);
        float base_u = fmax(fabs(@unitdist), 1e-6f) * @y_bias;   // <-- Y bias applied here

        for (int x = lane; x < xr; x += yr)
        {
            // TOP → BOTTOM
            int2 p = (int2)(x, 0);
            float val = @dst.bufferIndex(p);

            for (int y = 1; y < yr; ++y)
            {
                p = (int2)(x, y);
                float h = @dst.bufferIndex(p);
                float raw = @src.bufferIndex(p);
                float depth = fabs(h - raw);

                int iid = @id.bufferIndex(p);
                float scale = id_unitdist_scale(iid, @seed, @unitdist_id_lerp);
                float voronoi = clamp(@row_distance.bufferIndex(p), 0.0f, 1.0f);
                float y_pattern = @y_bias_voronoi_invert ? 1.0f - voronoi : voronoi;
                float y_scale = fmax(1.0f + (y_pattern * 2.0f - 1.0f) * @y_bias_voronoi, 1e-6f);
                float u = fmax(base_u * scale * y_scale, 1e-6f);

                float modulation = clamp(depth / u, 0.0f, 1.0f);
                float carve_scale = fmax(0.0f, 1.0f + (voronoi * 2.0f - 1.0f) * @carve_voronoi);
                float falloff = (dy / u) * fmax(0.05f, 1.0f +
                    (modulation * 2.0f - 1.0f) * @carve_depth * carve_scale);

                if (@dir)
                {
                    val -= falloff;
                    val = fmax(val, h);
                }
                else
                {
                    val += falloff;
                    val = fmin(val, h);
                }

                @dst.setIndex(p, val);
            }

            // BOTTOM → TOP
            p = (int2)(x, yr - 1);
            val = @dst.bufferIndex(p);

            for (int y = yr - 2; y >= 0; --y)
            {
                p = (int2)(x, y);
                float h = @dst.bufferIndex(p);
                float raw = @src.bufferIndex(p);
                float depth = fabs(h - raw);

                int iid = @id.bufferIndex(p);
                float scale = id_unitdist_scale(iid, @seed, @unitdist_id_lerp);
                float bottom_weight = (float)y / (float)max(yr - 1, 1);
                float voronoi = clamp(@row_distance.bufferIndex(p), 0.0f, 1.0f);
                float y_pattern = @y_bias_voronoi_invert ? 1.0f - voronoi : voronoi;
                float y_scale = fmax(1.0f + (y_pattern * 2.0f - 1.0f) * @y_bias_voronoi, 1e-6f);
                float u = fmax(base_u * scale * y_scale *
                               (1.0f - @neg_y_unitdist_taper * bottom_weight), 1e-6f);

                float modulation = clamp(depth / u, 0.0f, 1.0f);
                float carve_scale = fmax(0.0f, 1.0f + (voronoi * 2.0f - 1.0f) * @carve_voronoi);
                float falloff = (dy / u) * fmax(0.05f, 1.0f +
                    (modulation * 2.0f - 1.0f) * @carve_depth * carve_scale);

                if (@dir)
                {
                    val -= falloff;
                    val = fmax(val, h);
                    val = fmax(val, @dst.bufferIndex(p));
                }
                else
                {
                    val += falloff;
                    val = fmin(val, h);
                    val = fmin(val, @dst.bufferIndex(p));
                }

                @dst.setIndex(p, val);
            }
        }
        return;
    }

    // ========================================================
    // PASS 4 – Seed distance from ID crossings
    // ========================================================
    if (@Iteration == 4)
    {
        int2 p = @ixy;
        int iid = @id.bufferIndex(p);
        int row = @row_id.bufferIndex(p);
        float2 dxy = (float2)(fabs(@dPdx.x), fabs(@dPdy.y));
        float distance = 60000.0f;
        float row_distance = 60000.0f;
        for (int yy = -1; yy <= 1; ++yy)
        {
            for (int xx = -1; xx <= 1; ++xx)
            {
                if (xx == 0 && yy == 0) continue;
                int2 q = (int2)(wrapi(p.x + xx, xr), wrapi(p.y + yy, yr));
                float crossing = 0.5f * length(convert_float2((int2)(xx, yy)) * dxy);
                int other_row = @row_id.bufferIndex(q);
                if (@id.bufferIndex(q) != iid && (row < 0 || other_row < 0 || other_row == row))
                    distance = fmin(distance, crossing);
                if (row >= 0 && other_row >= 0 && other_row != row)
                    row_distance = fmin(row_distance, crossing);
            }
        }
        @id_distance.set(distance);
        @row_distance.set(row_distance);
        return;
    }

    // ========================================================
    // PASS 5 – Horizontal ID-boundary distance
    // ========================================================
    if (@Iteration == 5)
    {
        int y = @iy;
        if (y >= yr) return;
        float dx = fabs(@dPdx.x);
        float distance = 60000.0f;
        float row_distance = 60000.0f;
        for (int x = 0; x < xr; ++x)
        {
            int2 p = (int2)(x, y);
            distance = fmin(@id_distance.bufferIndex(p), distance + dx);
            row_distance = fmin(@row_distance.bufferIndex(p), row_distance + dx);
            @id_distance.setIndex(p, distance);
            @row_distance.setIndex(p, row_distance);
        }
        distance = 60000.0f;
        row_distance = 60000.0f;
        for (int x = xr - 1; x >= 0; --x)
        {
            int2 p = (int2)(x, y);
            distance = fmin(@id_distance.bufferIndex(p), distance + dx);
            row_distance = fmin(@row_distance.bufferIndex(p), row_distance + dx);
            @id_distance.setIndex(p, distance);
            @row_distance.setIndex(p, row_distance);
        }
        return;
    }

    // ========================================================
    // PASS 6 – Vertical ID-boundary distance
    // ========================================================
    if (@Iteration == 6)
    {
        int lane = @iy;
        float dy = fabs(@dPdy.y);
        for (int x = lane; x < xr; x += yr)
        {
            float distance = 60000.0f;
            float row_distance = 60000.0f;
            for (int y = 0; y < yr; ++y)
            {
                int2 p = (int2)(x, y);
                distance = fmin(@id_distance.bufferIndex(p), distance + dy);
                row_distance = fmin(@row_distance.bufferIndex(p), row_distance + dy);
                @id_distance.setIndex(p, distance);
                @row_distance.setIndex(p, row_distance);
            }
            distance = 60000.0f;
            row_distance = 60000.0f;
            for (int y = yr - 1; y >= 0; --y)
            {
                int2 p = (int2)(x, y);
                distance = fmin(@id_distance.bufferIndex(p), distance + dy);
                row_distance = fmin(@row_distance.bufferIndex(p), row_distance + dy);
                @id_distance.setIndex(p, distance);
                @row_distance.setIndex(p, row_distance);
            }
        }
        return;
    }

    // ========================================================
    // PASS 7 – Chamfer and ID/Voronoi cavities (reuse pass 0's raw field)
    // ========================================================
    if (@Iteration == 7)
    {
        int2 p = @ixy;
        float a = @dst.bufferIndex(p);
        float b = @src.bufferIndex(p);
        float plain = fmin(a, b);
        float width = fmax(@block_cavity_width, 0.0f);
        float row_width = fmax(@row_cavity_width, 0.0f);
        float cavity_active = (width > 0.0f || row_width > 0.0f) && @cavity_intensity != 0.0f;
        float voronoi_seam = 0.0f;
        if (cavity_active || @chamfer_voronoi != 0.0f)
            voronoi_seam = 1.0f - @voronoi_raw.bufferIndex(p);
        float chamfer_radius = @chamfer_width * fmax(0.0f, 1.0f +
            (voronoi_seam * 2.0f - 1.0f) * @chamfer_voronoi);
        float beveled = sdfOpUnionChamfer(a, b, chamfer_radius);
        float result = plain + (beveled - plain) * @chamfer_intensity;

        if (cavity_active)
        {
            float mask = clamp((voronoi_seam - @cavity_voronoi_threshold) *
                               @cavity_voronoi_mask_gain, 0.0f, 1.0f);
            float block_width = width * mask;
            float effective_row_width = row_width * mask;
            float block_cavity = block_width > 0.0f
                ? fmax(block_width - @id_distance.bufferIndex(p), 0.0f) : 0.0f;
            float row_cavity = effective_row_width > 0.0f
                ? 2.0f * fmax(effective_row_width - @row_distance.bufferIndex(p), 0.0f) : 0.0f;
            result -= @cavity_intensity * fmax(block_cavity, row_cavity);
        }
        @dst.set(result);
        return;
    }
}